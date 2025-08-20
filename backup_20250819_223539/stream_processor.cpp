// stream_processor.cpp - 流式处理核心实现（完整修复版）
#include "stream_processor.h"
#include "clang_frontend.h"
#include "compilation_database.h"
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/ADT/SmallString.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <algorithm>

// 静态成员变量定义
std::unique_ptr<CompilationDatabaseProcessor> SingleFileAnalyzer::shared_db_processor = nullptr;
std::mutex SingleFileAnalyzer::db_processor_mutex;

//=============================================================================
// StreamProcessor 实现
//=============================================================================

StreamProcessor::StreamProcessor(size_t num_threads) 
    : max_worker_threads(num_threads), max_memory_usage(500 * 1024 * 1024) {
    worker_threads.reserve(max_worker_threads);
}

StreamProcessor::~StreamProcessor() {
    stop();
}

void StreamProcessor::setCallback(std::unique_ptr<IStreamingCallback> cb) {
    callback = std::move(cb);
}

void StreamProcessor::setMaxMemoryUsage(size_t max_bytes) {
    max_memory_usage = max_bytes;
}

void StreamProcessor::setCompileDbPath(const std::string& path) {
    compile_db_path = path;
}

void StreamProcessor::start() {
    should_stop = false;
    processed_files = 0;
    cache_hits = 0;
    error_count = 0;
    
    // 启动工作线程
    for (size_t i = 0; i < max_worker_threads; ++i) {
        worker_threads.emplace_back(&StreamProcessor::workerLoop, this);
    }
    
    std::cout << "🚀 流式处理器已启动，工作线程数: " << max_worker_threads << std::endl;
}

void StreamProcessor::stop() {
    should_stop = true;
    queue_cv.notify_all();
    
    // 等待所有工作线程完成
    for (auto& thread : worker_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads.clear();
    
    if (callback) {
        callback->onComplete();
    }
}

void StreamProcessor::addTask(const StreamingTask& task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        task_queue.push(task);
        total_files++;
    }
    queue_cv.notify_one();
}

void StreamProcessor::addTasks(const std::vector<StreamingTask>& tasks) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        for (const auto& task : tasks) {
            task_queue.push(task);
        }
        total_files += tasks.size();
    }
    queue_cv.notify_all();
}

void StreamProcessor::workerLoop() {
    while (!should_stop) {
        StreamingTask task;
        
        // 获取任务
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, [this] { return !task_queue.empty() || should_stop; });
            
            if (should_stop) break;
            if (task_queue.empty()) continue;
            
            task = task_queue.top();
            task_queue.pop();
        }
        
        // 处理任务
        bool success = processTask(task);
        processed_files++;
        
        if (!success) {
            error_count++;
        }
        
        // 报告进度
        if (callback) {
            float progress = static_cast<float>(processed_files) / total_files;
            callback->onProgress(progress, task.file_path);
        }
        
        // 内存管理
        manageMemory();
    }
}

bool StreamProcessor::processTask(const StreamingTask& task) {
    // 检查是否需要重新分析
    if (!needsReanalysis(task)) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        auto cache_it = cached_results.find(task.file_path);
        if (cache_it != cached_results.end() && callback) {
            cache_hits++;
            callback->onFileResult(cache_it->second);
            return true;
        }
    }
    
    // 执行实际分析
    FileAnalysisResult result;
    result.file_path = task.file_path;
    
    SingleFileAnalyzer analyzer(task.file_path, compile_db_path);
    if (analyzer.analyze()) {
        analyzer.extractResults(result);
        result.success = true;
        
        // 缓存结果
        {
            std::lock_guard<std::mutex> lock(cache_mutex);
            cached_results[task.file_path] = result;
            current_memory_usage += estimateResultSize(result);
        }
        
        // 发送结果
        if (callback) {
            callback->onFileResult(result);
        }
        
        return true;
    } else {
        result.success = false;
        result.error_message = "Analysis failed for " + task.file_path;
        
        if (callback) {
            callback->onError(task.file_path, result.error_message);
        }
        return false;
    }
}

bool StreamProcessor::needsReanalysis(const StreamingTask& task) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    auto hash_it = file_hashes.find(task.file_path);
    if (hash_it == file_hashes.end() || hash_it->second != task.content_hash) {
        file_hashes[task.file_path] = task.content_hash;
        return true;
    }
    
    return false;
}

std::string StreamProcessor::calculateFileHash(const std::string& file_path) {
    llvm::sys::fs::file_status status;
    if (llvm::sys::fs::status(file_path, status)) {
        return "";
    }
    
    auto size = status.getSize();
    auto time = std::chrono::duration_cast<std::chrono::seconds>(
        status.getLastModificationTime().time_since_epoch()).count();
    
    return std::to_string(size) + "_" + std::to_string(time);
}

void StreamProcessor::manageMemory() {
    if (current_memory_usage > max_memory_usage) {
        evictOldResults();
    }
}

void StreamProcessor::evictOldResults() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    
    // 简单的 LRU 策略：移除一些旧的缓存结果
    size_t target_size = max_memory_usage * 0.8;
    
    auto it = cached_results.begin();
    while (it != cached_results.end() && current_memory_usage > target_size) {
        current_memory_usage -= estimateResultSize(it->second);
        it = cached_results.erase(it);
    }
}

size_t StreamProcessor::estimateResultSize(const FileAnalysisResult& result) {
    size_t size = 0;
    size += result.writes.size() * sizeof(WriteOperation);
    size += result.calls.size() * sizeof(CallInfo);
    size += result.fp_calls.size() * sizeof(FunctionPointerCall);
    size += result.register_ops.size() * sizeof(RegisterOperation);
    size += result.functions.size() * 200; // 估算函数信息大小
    size += result.global_vars.size() * 50; // 估算字符串大小
    return size;
}

float StreamProcessor::getProgress() const {
    if (total_files == 0) return 0.0f;
    return static_cast<float>(processed_files) / total_files;
}

size_t StreamProcessor::getCacheHitRate() const {
    if (processed_files == 0) return 0;
    return (cache_hits * 100) / processed_files;
}

size_t StreamProcessor::getCurrentMemoryUsage() const {
    return current_memory_usage;
}

//=============================================================================
// SingleFileAnalyzer 实现 - 修复路径和编译器问题
//=============================================================================

SingleFileAnalyzer::SingleFileAnalyzer(const std::string& file, const std::string& compile_db)
    : file_path(file), compile_db_path(compile_db), local_data(std::make_unique<AnalysisData>()) {}

CompilationDatabaseProcessor* SingleFileAnalyzer::getSharedDbProcessor(const std::string& compile_db_path) {
    std::lock_guard<std::mutex> lock(db_processor_mutex);
    
    if (!shared_db_processor) {
        shared_db_processor = std::make_unique<CompilationDatabaseProcessor>(compile_db_path);
        if (!shared_db_processor->loadCompileCommands()) {
            shared_db_processor.reset();
            return nullptr;
        }
    }
    
    return shared_db_processor.get();
}

bool SingleFileAnalyzer::analyze() {
    std::vector<std::string> args;
    if (!setupCompilationArgs(args)) {
        return false;
    }
    
    return runClangAnalysis(args);
}

bool SingleFileAnalyzer::setupCompilationArgs(std::vector<std::string>& args) {
    // 首先解析文件路径
    std::string resolved_path = resolveFilePath(file_path);
    if (resolved_path.empty()) {
        return false;
    }
    
    // 更新文件路径为解析后的路径
    const_cast<std::string&>(file_path) = resolved_path;
    
    // 使用编译数据库查找编译参数
    if (!compile_db_path.empty()) {
        auto* db_processor = getSharedDbProcessor(compile_db_path);
        if (db_processor) {
            if (findCompilationArgsFromDb(db_processor, args)) {
                return true;
            }
        }
    }
    
    // 如果没有找到编译数据库或特定文件的编译参数，使用默认参数
    return createDefaultCompilationArgs(args);
}

std::string SingleFileAnalyzer::resolveFilePath(const std::string& original_path) {
    // 如果文件存在，直接返回
    if (llvm::sys::fs::exists(original_path)) {
        return original_path;
    }
    
    std::cout << "🔍 文件不存在，尝试解析路径: " << original_path << std::endl;
    
    // 策略1: 处理绝对路径，尝试转换为相对路径
    if (llvm::sys::path::is_absolute(original_path)) {
        std::vector<std::string> search_patterns = {
            "kafl.linux", "linux", "kernel", "drivers", "src", "source"
        };
        
        for (const auto& pattern : search_patterns) {
            size_t pos = original_path.find(pattern);
            if (pos != std::string::npos) {
                std::string relative_part = original_path.substr(pos);
                
                // 尝试不同的基础路径
                std::vector<std::string> base_paths = {
                    "../",
                    "../../", 
                    "./",
                    "../" + pattern + "/"
                };
                
                for (const auto& base : base_paths) {
                    std::string candidate = base + relative_part;
                    if (llvm::sys::fs::exists(candidate)) {
                        std::cout << "✅ 找到文件: " << candidate << std::endl;
                        return candidate;
                    }
                }
            }
        }
    }
    
    // 策略2: 尝试在常见目录中查找文件名
    std::string filename = llvm::sys::path::filename(original_path).str();
    std::vector<std::string> search_dirs = {
        "./",
        "../",
        "../../",
        "../kafl.linux/",
        "../kafl.linux/drivers/",
        "../kafl.linux/drivers/media/",
        "../kafl.linux/drivers/media/rc/",
        "../kafl.linux/drivers/media/rc/keymaps/"
    };
    
    for (const auto& dir : search_dirs) {
        std::string candidate = dir + filename;
        if (llvm::sys::fs::exists(candidate)) {
            std::cout << "✅ 找到文件: " << candidate << std::endl;
            return candidate;
        }
    }
    
    // 策略3: 递归搜索 (限制深度)
    std::string found_path = recursiveFileSearch(".", filename, 3);
    if (!found_path.empty()) {
        std::cout << "✅ 递归找到文件: " << found_path << std::endl;
        return found_path;
    }
    
    std::cout << "❌ 无法找到文件: " << original_path << std::endl;
    return "";
}

std::string SingleFileAnalyzer::recursiveFileSearch(const std::string& dir, 
                                                   const std::string& filename, 
                                                   int max_depth) {
    if (max_depth <= 0) return "";
    
    std::error_code ec;
    for (llvm::sys::fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        std::string path = it->path();
        
        if (llvm::sys::fs::is_directory(path)) {
            std::string result = recursiveFileSearch(path, filename, max_depth - 1);
            if (!result.empty()) {
                return result;
            }
        } else if (llvm::sys::path::filename(path) == filename) {
            return path;
        }
    }
    
    return "";
}

bool SingleFileAnalyzer::findCompilationArgsFromDb(CompilationDatabaseProcessor* db_processor,
                                                   std::vector<std::string>& args) {
    const auto& commands = db_processor->getCommands();
    
    // 尝试精确匹配
    for (const auto& cmd : commands) {
        if (cmd.file == file_path || 
            llvm::sys::path::filename(cmd.file) == llvm::sys::path::filename(file_path)) {
            
            args = cmd.arguments;
            
            // 关键修复：检查并替换无效的编译器路径
            if (!args.empty()) {
                std::string& compiler = args[0];
                
                // 检查编译器路径是否存在
                if (!llvm::sys::fs::exists(compiler)) {
                    std::cout << "⚠️ 编译器路径无效: " << compiler << std::endl;
                    
                    // 尝试找到有效的编译器
                    std::vector<std::string> fallback_compilers = {
                        "clang++",
                        "clang",
                        "g++",
                        "gcc",
                        "/usr/bin/clang++",
                        "/usr/bin/clang",
                        "/usr/bin/g++",
                        "/usr/bin/gcc"
                    };
                    
                    bool found_compiler = false;
                    for (const auto& fallback : fallback_compilers) {
                        if (llvm::sys::fs::can_execute(fallback) || 
                            (system(("which " + fallback + " > /dev/null 2>&1").c_str()) == 0)) {
                            std::cout << "✅ 使用替代编译器: " << fallback << std::endl;
                            compiler = fallback;
                            found_compiler = true;
                            break;
                        }
                    }
                    
                    if (!found_compiler) {
                        std::cout << "❌ 找不到有效的编译器" << std::endl;
                        return false;
                    }
                }
            }
            
            cleanupCompilationArgs(args);
            replaceFilePathInArgs(args, cmd.file);
            
            std::cout << "✅ 找到匹配的编译命令（已修复编译器路径）" << std::endl;
            return true;
        }
    }
    
    // 如果没有精确匹配，使用通用选项
    args = db_processor->getCommonCompileOptions();
    args.insert(args.begin(), "clang++"); // 添加默认编译器
    args.push_back(file_path);
    
    std::cout << "⚠️ 使用通用编译选项" << std::endl;
    return true;
}

void SingleFileAnalyzer::cleanupCompilationArgs(std::vector<std::string>& args) {
    // 移除第一个参数（编译器名称），我们会重新设置
    if (!args.empty()) {
        args.erase(args.begin());
    }
    
    // 移除输出文件和问题参数
    auto it = args.begin();
    while (it != args.end()) {
        const std::string& arg = *it;
        
        // 移除输出相关参数
        if (arg == "-o") {
            it = args.erase(it);
            if (it != args.end()) {
                it = args.erase(it);
            }
        }
        // 移除编译相关参数（我们只需要分析，不需要编译）
        else if (arg == "-c") {
            it = args.erase(it);
        }
        // 移除可能导致问题的架构特定参数
        else if (arg.find("-march=") != std::string::npos ||
                 arg.find("-mcpu=") != std::string::npos ||
                 arg.find("-mtune=") != std::string::npos) {
            it = args.erase(it);
        }
        // 移除链接相关参数
        else if (arg.find("-Wl,") == 0 || 
                 arg == "-shared" || 
                 arg == "-static" ||
                 arg.find("-l") == 0) {
            it = args.erase(it);
        }
        // 移除可能导致问题的其他参数
        else if (arg == "-pipe" ||
                 arg.find("-flto") == 0 ||
                 arg == "-fno-semantic-interposition" ||
                 arg.find("--target=") == 0) {
            it = args.erase(it);
        }
        else {
            ++it;
        }
    }
}

void SingleFileAnalyzer::replaceFilePathInArgs(std::vector<std::string>& args, 
                                              const std::string& original_file) {
    for (auto& arg : args) {
        if (arg == original_file || 
            llvm::sys::path::filename(arg) == llvm::sys::path::filename(file_path)) {
            arg = file_path;
            break;
        }
    }
    
    // 如果没有找到文件参数，添加它
    bool found_file = false;
    for (const auto& arg : args) {
        if (arg == file_path) {
            found_file = true;
            break;
        }
    }
    
    if (!found_file) {
        args.push_back(file_path);
    }
}

bool SingleFileAnalyzer::createDefaultCompilationArgs(std::vector<std::string>& args) {
    args = {
        "-std=c99",        // 使用 C99 标准而不是 C++17
        "-w",              // 抑制所有警告
        "-fno-builtin",    // 禁用内建函数
        "-nostdinc",       // 不使用标准头文件路径
        "-I/usr/include",
        "-I/usr/local/include",
        file_path
    };
    
    std::cout << "⚠️ 使用默认 C 分析参数" << std::endl;
    return true;
}

bool SingleFileAnalyzer::runClangAnalysis(const std::vector<std::string>& args) {
    try {
        // 创建清理过的参数列表
        std::vector<std::string> clean_args = args;
        
        // 确保第一个参数不是编译器路径
        if (!clean_args.empty() && 
            (clean_args[0].find("clang") != std::string::npos || 
             clean_args[0].find("gcc") != std::string::npos)) {
            clean_args.erase(clean_args.begin());
        }
        
        // 创建工作目录
        std::string working_dir = llvm::sys::path::parent_path(file_path).str();
        if (working_dir.empty()) {
            working_dir = ".";
        }
        
        std::cout << "🔧 使用工作目录: " << working_dir << std::endl;
        std::cout << "📝 编译参数数量: " << clean_args.size() << std::endl;
        
        // 创建固定的编译数据库
        clang::tooling::FixedCompilationDatabase db(working_dir, clean_args);
        clang::tooling::ClangTool tool(db, {file_path});
        
        // 关键修复：更严格的参数调整器
        tool.appendArgumentsAdjuster([](const clang::tooling::CommandLineArguments& args, llvm::StringRef filename) {
            clang::tooling::CommandLineArguments adjusted_args;
            
            std::cout << "🔧 原始参数数量: " << args.size() << std::endl;
            
            for (size_t i = 0; i < args.size(); ++i) {
                const auto& arg = args[i];
                
                // 跳过编译器路径（通常是第一个参数）
                if (i == 0 && (arg.find("clang") != std::string::npos || 
                              arg.find("gcc") != std::string::npos)) {
                    continue;
                }
                
                // 跳过问题参数
                if (arg.find("--help") == 0 || 
                    arg.find("--version") == 0 ||
                    arg.find("-march=") != std::string::npos ||
                    arg.find("-mcpu=") != std::string::npos ||
                    arg.find("-mtune=") != std::string::npos ||
                    arg == "-pipe" || 
                    arg.find("-flto") == 0 ||
                    arg == "-fno-semantic-interposition" ||
                    arg.find("--target=") == 0 ||
                    arg.find("-Wl,") == 0 ||
                    arg == "-shared" ||
                    arg == "-static") {
                    std::cout << "⚠️ 跳过参数: " << arg << std::endl;
                    continue;
                }
                
                // 处理 -o 参数（跳过输出文件）
                if (arg == "-o" && i + 1 < args.size()) {
                    std::cout << "⚠️ 跳过输出参数: " << arg << " " << args[i + 1] << std::endl;
                    ++i; // 跳过下一个参数（输出文件名）
                    continue;
                }
                
                adjusted_args.push_back(arg);
            }
            
            // 确保有警告抑制标志
            bool has_warning_suppression = false;
            for (const auto& arg : adjusted_args) {
                if (arg == "-w" || arg == "-Wno-all") {
                    has_warning_suppression = true;
                    break;
                }
            }
            if (!has_warning_suppression) {
                adjusted_args.push_back("-w");
            }
            
            // 确保有文件参数
            bool has_file = false;
            for (const auto& arg : adjusted_args) {
                if (arg == filename.str()) {
                    has_file = true;
                    break;
                }
            }
            if (!has_file) {
                adjusted_args.push_back(filename.str());
            }
            
            std::cout << "✅ 调整后参数数量: " << adjusted_args.size() << std::endl;
            
            return adjusted_args;
        });
        
        // 创建分析动作工厂
        InterruptAnalysisActionFactory factory(local_data.get());
        
        // 运行分析
        std::cout << "🚀 开始 Clang 分析..." << std::endl;
        int result = tool.run(&factory);
        
        if (result != 0) {
            std::cout << "⚠️ Clang 分析返回代码: " << result 
                      << " (文件: " << llvm::sys::path::filename(file_path).str() << ")" << std::endl;
            return false; // 改为返回 false，这样可以跳过有问题的文件
        }
        
        std::cout << "✅ Clang 分析成功" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cout << "❌ 分析异常: " << e.what() 
                  << " (文件: " << llvm::sys::path::filename(file_path).str() << ")" << std::endl;
        return false;
    }
}

void SingleFileAnalyzer::extractResults(FileAnalysisResult& result) {
    if (!local_data) return;
    
    std::lock_guard<std::mutex> lock(local_data->mutex);
    
    // 提取分析结果
    result.writes = local_data->all_writes;
    result.calls = local_data->all_calls;
    result.fp_calls = local_data->function_pointer_calls;
    result.register_ops = local_data->register_ops;
    result.parameter_sources = local_data->parameter_sources;
    result.return_values = local_data->return_values;
    result.functions = local_data->function_signatures;
    result.global_vars = local_data->global_variables;
    result.pointer_aliases = local_data->pointer_aliases;
}

//=============================================================================
// FileResultAggregator 实现
//=============================================================================

FileResultAggregator::FileResultAggregator(AnalysisData* data, size_t batch_size, size_t memory_limit)
    : target_data(data), batch_size_threshold(batch_size), max_memory_threshold(memory_limit) {}

void FileResultAggregator::onFileResult(const FileAnalysisResult& result) {
    std::lock_guard<std::mutex> lock(aggregation_mutex);
    
    pending_batch.push_back(result);
    current_batch_size += estimateResultSize(result);
    processed_count++;
    
    // 检查是否需要处理批次
    if (pending_batch.size() >= batch_size_threshold || 
        current_batch_size >= max_memory_threshold) {
        processBatch();
    }
}

void FileResultAggregator::onProgress(float progress, const std::string& current_file) {
    // 进度报告 - 每5%报告一次，减少输出噪音
    static int last_percent = -1;
    int current_percent = static_cast<int>(progress * 100);
    
    if (current_percent != last_percent && current_percent % 5 == 0) {
        std::cout << "📊 流式分析进度: " << current_percent << "% [" 
                  << llvm::sys::path::filename(current_file).str() << "]" << std::endl;
        last_percent = current_percent;
    }
}

void FileResultAggregator::onError(const std::string& file_path, const std::string& error) {
    error_count++;
    
    // 减少错误输出的频率，避免刷屏
    static bool debug_mode = std::getenv("DEBUG_ANALYZER") != nullptr;
    
    if (debug_mode) {
        std::cerr << "❌ 文件分析错误 [" << llvm::sys::path::filename(file_path).str() 
                  << "]: " << error << std::endl;
    } else {
        // 每20个错误显示一次汇总
        if (error_count % 20 == 1) {
            std::cout << "⚠️ 已跳过 " << error_count << " 个无法分析的文件..." << std::endl;
        }
    }
}

void FileResultAggregator::onComplete() {
    std::lock_guard<std::mutex> lock(aggregation_mutex);
    flushPendingResults();
    
    std::cout << "✅ 流式处理完成!" << std::endl;
    std::cout << "   成功处理: " << processed_count << " 个文件" << std::endl;
    
    if (error_count > 0) {
        std::cout << "   跳过文件: " << error_count << " 个（编译错误或路径问题）" << std::endl;
        std::cout << "   💡 提示: 设置环境变量 DEBUG_ANALYZER=1 查看详细错误信息" << std::endl;
    }
}

void FileResultAggregator::processBatch() {
    for (const auto& result : pending_batch) {
        if (result.success) {
            aggregateResult(result);
        }
    }
    
    // 清理批次
    pending_batch.clear();
    current_batch_size = 0;
}

void FileResultAggregator::flushPendingResults() {
    if (!pending_batch.empty()) {
        processBatch();
    }
}

void FileResultAggregator::aggregateResult(const FileAnalysisResult& result) {
    // 聚合各种分析结果到目标数据结构
    for (const auto& write : result.writes) {
        target_data->addWrite(write);
    }
    
    for (const auto& call : result.calls) {
        target_data->addCallInfo(call);
    }
    
    for (const auto& fp_call : result.fp_calls) {
        target_data->addFunctionPointerCall(fp_call);
    }
    
    for (const auto& reg_op : result.register_ops) {
        target_data->addRegisterOp(reg_op);
    }
    
    for (const auto& param_src : result.parameter_sources) {
        target_data->addParameterSource(param_src);
    }
    
    for (const auto& ret_val : result.return_values) {
        target_data->addReturnValue(ret_val);
    }
    
    for (const auto& [key, func_info] : result.functions) {
        target_data->addFunctionSignature(key, func_info);
        target_data->addFunctionLocation(func_info.name, func_info.file);
    }
    
    for (const auto& [var_name, file] : result.global_vars) {
        target_data->addGlobalVariable(var_name, file);
    }
    
    for (const auto& [alias, global_path] : result.pointer_aliases) {
        target_data->addPointerAlias(alias, global_path);
    }
}

size_t FileResultAggregator::estimateResultSize(const FileAnalysisResult& result) {
    size_t size = 0;
    size += result.writes.size() * sizeof(WriteOperation);
    size += result.calls.size() * sizeof(CallInfo);
    size += result.fp_calls.size() * sizeof(FunctionPointerCall);
    size += result.register_ops.size() * sizeof(RegisterOperation);
    size += result.parameter_sources.size() * sizeof(ParameterSource);
    size += result.return_values.size() * sizeof(ReturnValueInfo);
    
    // 估算字符串和容器的开销
    for (const auto& [key, func] : result.functions) {
        size += key.length() + func.name.length() + func.file.length();
        for (const auto& param : func.parameters) {
            size += param.length();
        }
    }
    
    for (const auto& [var, file] : result.global_vars) {
        size += var.length() + file.length();
    }
    
    for (const auto& [alias, path] : result.pointer_aliases) {
        size += alias.length() + path.length();
    }
    
    return size;
}

//=============================================================================
// TaskScheduler 实现 - 改进文件过滤
//=============================================================================

std::vector<StreamingTask> TaskScheduler::createTasks(const std::vector<std::string>& files) {
    std::vector<StreamingTask> tasks;
    tasks.reserve(files.size());
    
    size_t skipped_count = 0;
    size_t valid_count = 0;
    
    for (const auto& file : files) {
        // 跳过明显无法分析的文件
        if (shouldSkipFile(file)) {
            skipped_count++;
            continue;
        }
        
        StreamingTask task;
        task.file_path = file;
        task.content_hash = calculateFileHash(file);
        
        // 获取文件大小
        llvm::sys::fs::file_status status;
        if (!llvm::sys::fs::status(file, status)) {
            task.file_size = status.getSize();
        } else {
            task.file_size = 0;
        }
        
        task.priority = calculateFilePriority(file, task.file_size);
        tasks.push_back(task);
        valid_count++;
    }
    
    std::cout << "📋 文件过滤统计:" << std::endl;
    std::cout << "   输入文件数: " << files.size() << std::endl;
    std::cout << "   有效 C/C++ 文件: " << valid_count << std::endl;
    std::cout << "   跳过文件: " << skipped_count << std::endl;
    
    return tasks;
}

int TaskScheduler::calculateFilePriority(const std::string& file_path, size_t file_size) {
    int priority = 100; // 基础优先级
    
    // 根据文件大小调整优先级（小文件优先）
    if (file_size < 10000) {
        priority -= 20;
    } else if (file_size > 100000) {
        priority += 20;
    }
    
    // 关键路径文件优先级更高
    if (isCriticalPath(file_path)) {
        priority -= 30;
    }
    
    return priority;
}

bool TaskScheduler::isCriticalPath(const std::string& file_path) {
    std::vector<std::string> critical_patterns = {
        "/drivers/", "/kernel/", "/mm/", "/fs/", "/net/",
        "interrupt", "irq", "timer", "sched"
    };
    
    std::string lower_path = file_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    
    for (const auto& pattern : critical_patterns) {
        if (lower_path.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

std::string TaskScheduler::calculateFileHash(const std::string& file_path) {
    llvm::sys::fs::file_status status;
    if (llvm::sys::fs::status(file_path, status)) {
        return "";
    }
    
    auto size = status.getSize();
    auto time = std::chrono::duration_cast<std::chrono::seconds>(
        status.getLastModificationTime().time_since_epoch()).count();
    
    return std::to_string(size) + "_" + std::to_string(time);
}

bool TaskScheduler::shouldSkipFile(const std::string& file_path) {
    // 只处理 C/C++ 源文件
    if (!isCppSourceFile(file_path)) {
        return true;
    }
    
    // 跳过明显的测试文件和示例文件
    std::string lower_path = file_path;
    std::transform(lower_path.begin(), lower_path.end(), lower_path.begin(), ::tolower);
    
    std::vector<std::string> skip_patterns = {
        "/test/", "/tests/", "/example/", "/examples/", 
        "/sample/", "/samples/", "/demo/", "/demos/",
        "_test.c", "_test.cpp", "test_", "example_"
    };
    
    for (const auto& pattern : skip_patterns) {
        if (lower_path.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

bool TaskScheduler::isCppSourceFile(const std::string& file_path) {
    std::string ext = llvm::sys::path::extension(file_path).str();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static const std::vector<std::string> valid_extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".C"
    };
    
    return std::find(valid_extensions.begin(), valid_extensions.end(), ext) != valid_extensions.end();
}
