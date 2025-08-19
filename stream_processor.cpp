// stream_processor.cpp - 流式处理核心实现
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

// 静态成员变量定义 - 必须在全局作用域中定义
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
// SingleFileAnalyzer 实现
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
    // 首先检查文件是否存在
    if (!llvm::sys::fs::exists(file_path)) {
        // 如果文件不存在，尝试相对于编译数据库目录查找
        if (!compile_db_path.empty()) {
            auto* db_processor = getSharedDbProcessor(compile_db_path);
            if (db_processor) {
                const auto& commands = db_processor->getCommands();
                for (const auto& cmd : commands) {
                    if (cmd.file == file_path) {
                        // 尝试使用编译命令中的目录
                        std::string alternative_path = cmd.directory + "/" + llvm::sys::path::filename(file_path).str();
                        if (llvm::sys::fs::exists(alternative_path)) {
                            // 更新文件路径
                            const_cast<std::string&>(file_path) = alternative_path;
                            break;
                        }
                    }
                }
            }
        }
        
        // 如果仍然找不到文件，返回失败
        if (!llvm::sys::fs::exists(file_path)) {
            return false;
        }
    }
    
    // 使用共享的编译数据库处理器
    if (!compile_db_path.empty()) {
        auto* db_processor = getSharedDbProcessor(compile_db_path);
        if (db_processor) {
            // 查找这个文件的编译命令
            const auto& commands = db_processor->getCommands();
            for (const auto& cmd : commands) {
                if (cmd.file == file_path || 
                    llvm::sys::path::filename(cmd.file) == llvm::sys::path::filename(file_path)) {
                    
                    // 找到了对应的编译命令，使用其参数
                    args = cmd.arguments;
                    
                    // 移除编译器名称（通常是第一个参数）
                    if (!args.empty() && (args[0].find("gcc") != std::string::npos || 
                                         args[0].find("clang") != std::string::npos ||
                                         args[0].find("cc") != std::string::npos)) {
                        args.erase(args.begin());
                    }
                    
                    // 移除输出文件相关的参数
                    auto it = args.begin();
                    while (it != args.end()) {
                        if (*it == "-o") {
                            it = args.erase(it);
                            // 删除输出文件名
                            if (it != args.end()) {
                                it = args.erase(it);
                            }
                        } else if (*it == "-c") {
                            it = args.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    
                    // 修复相对路径问题 - 添加工作目录
                    std::string working_dir = cmd.directory;
                    if (!working_dir.empty()) {
                        // 添加工作目录到包含路径
                        args.insert(args.begin(), "-I" + working_dir);
                        args.insert(args.begin(), "-I" + working_dir + "/include");
                    }
                    
                    // 确保文件路径正确
                    bool found_file_arg = false;
                    for (auto& arg : args) {
                        if (arg == cmd.file || llvm::sys::path::filename(arg) == llvm::sys::path::filename(file_path)) {
                            arg = file_path; // 使用实际存在的文件路径
                            found_file_arg = true;
                            break;
                        }
                    }
                    if (!found_file_arg) {
                        args.push_back(file_path);
                    }
                    
                    return true;
                }
            }
            
            // 如果没找到特定文件的命令，使用通用选项
            auto common_opts = db_processor->getCommonCompileOptions();
            args = common_opts;
            args.push_back(file_path);
            return true;
        }
    }
    
    // 如果没有编译数据库，使用最小的参数集
    args = {
        "-std=c++17",
        "-w",  // 抑制警告
        file_path
    };
    
    return true;
}

bool SingleFileAnalyzer::runClangAnalysis(const std::vector<std::string>& args) {
    // 创建固定的编译数据库 - 使用正确的构造方法
    std::string working_dir = ".";
    clang::tooling::FixedCompilationDatabase db(working_dir, args);
    clang::tooling::ClangTool tool(db, {file_path});
    
    // 添加参数调整器来处理问题参数
    tool.appendArgumentsAdjuster([](const clang::tooling::CommandLineArguments& args, llvm::StringRef) {
        clang::tooling::CommandLineArguments adjusted_args;
        for (const auto& arg : args) {
            // 跳过会导致clang工具问题的参数
            if (arg.find("--help") == 0 || arg.find("--version") == 0 ||
                arg.find("-march=native") != std::string::npos ||
                arg.find("-mcpu=native") != std::string::npos ||
                arg == "-pipe" || arg == "-fstack-protector-strong" ||
                arg.find("-flto") == 0 ||
                arg.find("-fno-semantic-interposition") == 0) {
                continue;
            }
            
            // 修复相对路径的包含文件
            if (arg.find("-I./include") == 0) {
                llvm::SmallString<256> current_path;
                if (!llvm::sys::fs::current_path(current_path)) {
                    adjusted_args.push_back("-I" + current_path.str().str() + "/include");
                } else {
                    adjusted_args.push_back(arg); // 如果获取当前路径失败，保持原参数
                }
                continue;
            }
            
            adjusted_args.push_back(arg);
        }
        
        // 确保有警告抑制标志
        bool has_w_flag = false;
        for (const auto& arg : adjusted_args) {
            if (arg == "-w" || arg == "-Wno-all") {
                has_w_flag = true;
                break;
            }
        }
        if (!has_w_flag) {
            adjusted_args.push_back("-w");
        }
        
        return adjusted_args;
    });
    
    // 创建分析动作工厂
    InterruptAnalysisActionFactory factory(local_data.get());
    
    // 运行分析
    int result = tool.run(&factory);
    return result == 0;
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
    // 进度报告 - 每10%报告一次
    static int last_percent = -1;
    int current_percent = static_cast<int>(progress * 100);
    
    if (current_percent != last_percent && current_percent % 10 == 0) {
        std::cout << "📊 流式分析进度: " << current_percent << "% [" 
                  << llvm::sys::path::filename(current_file).str() << "]" << std::endl;
        last_percent = current_percent;
    }
}

void FileResultAggregator::onError(const std::string& file_path, const std::string& error) {
    error_count++;
    
    // 只在调试模式下显示详细错误，否则只显示简要信息
    static bool debug_mode = std::getenv("DEBUG_ANALYZER") != nullptr;
    
    if (debug_mode) {
        std::cerr << "❌ 文件分析错误 [" << llvm::sys::path::filename(file_path).str() 
                  << "]: " << error << std::endl;
    } else {
        // 简要错误信息，避免刷屏
        if (error_count % 10 == 1) { // 每10个错误显示一次汇总
            std::cout << "⚠️ 跳过 " << error_count << " 个无法分析的文件..." << std::endl;
        }
    }
}

void FileResultAggregator::onComplete() {
    std::lock_guard<std::mutex> lock(aggregation_mutex);
    flushPendingResults();
    
    std::cout << "✅ 流式处理完成!" << std::endl;
    std::cout << "   成功处理: " << processed_count << " 个文件" << std::endl;
    
    if (error_count > 0) {
        std::cout << "   跳过文件: " << error_count << " 个（编译错误或不支持的文件）" << std::endl;
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
// TaskScheduler 实现
//=============================================================================

std::vector<StreamingTask> TaskScheduler::createTasks(const std::vector<std::string>& files) {
    std::vector<StreamingTask> tasks;
    tasks.reserve(files.size());
    
    size_t skipped_count = 0;
    size_t not_found_count = 0;
    
    for (const auto& file : files) {
        // 跳过明显无法分析的文件
        if (shouldSkipFile(file)) {
            skipped_count++;
            continue;
        }
        
        // 尝试解析文件路径
        std::string resolved_path = file;
        
        // 如果文件不存在，尝试不同的路径解析策略
        if (!llvm::sys::fs::exists(resolved_path)) {
            // 策略1: 尝试相对于 ../kafl.linux 的路径
            if (llvm::sys::path::is_absolute(file)) {
                // 如果是绝对路径，尝试转换为相对路径
                llvm::StringRef path_ref(file);
                
                // 查找 "kafl.linux" 或其他项目根目录标识
                size_t pos = path_ref.find("kafl.linux");
                if (pos != llvm::StringRef::npos) {
                    std::string relative_part = path_ref.substr(pos + 10).str(); // "kafl.linux"长度为10
                    std::string candidate = "../kafl.linux" + relative_part;
                    if (llvm::sys::fs::exists(candidate)) {
                        resolved_path = candidate;
                    }
                }
            } else {
                // 如果是相对路径，尝试添加项目根目录前缀
                std::vector<std::string> possible_bases = {
                    "../kafl.linux/",
                    "./",
                    "../"
                };
                
                for (const auto& base : possible_bases) {
                    std::string candidate = base + file;
                    if (llvm::sys::fs::exists(candidate)) {
                        resolved_path = candidate;
                        break;
                    }
                }
            }
            
            // 如果仍然找不到，跳过这个文件
            if (!llvm::sys::fs::exists(resolved_path)) {
                not_found_count++;
                // 在调试模式下显示找不到的文件
                static bool debug_mode = std::getenv("DEBUG_ANALYZER") != nullptr;
                if (debug_mode && not_found_count <= 10) { // 只显示前10个
                    std::cout << "⚠️ 文件不存在: " << file << std::endl;
                }
                continue;
            }
        }
        
        StreamingTask task;
        task.file_path = resolved_path;  // 使用解析后的路径
        task.content_hash = calculateFileHash(resolved_path);
        
        // 获取文件大小
        llvm::sys::fs::file_status status;
        if (!llvm::sys::fs::status(resolved_path, status)) {
            task.file_size = status.getSize();
        } else {
            task.file_size = 0;
        }
        
        task.priority = 100;
        tasks.push_back(task);
    }
    
    std::cout << "📋 文件过滤统计:" << std::endl;
    std::cout << "   总文件数: " << files.size() << std::endl;
    std::cout << "   有效 C/C++ 文件: " << tasks.size() << std::endl;
    std::cout << "   跳过（非源文件）: " << skipped_count << std::endl;
    std::cout << "   跳过（文件不存在）: " << not_found_count << std::endl;
    
    if (not_found_count > 10) {
        std::cout << "   💡 提示: 设置 DEBUG_ANALYZER=1 查看缺失文件详情" << std::endl;
    }
    
    return tasks;
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
    std::string ext = llvm::sys::path::extension(file_path).str();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    // 只接受这些扩展名的文件
    static const std::vector<std::string> valid_extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".C"
    };
    
    bool is_valid_ext = std::find(valid_extensions.begin(), valid_extensions.end(), ext) != valid_extensions.end();
    if (!is_valid_ext) {
        return true; // 跳过非 C/C++ 文件
    }
    
    // 检查文件是否实际存在
    if (!llvm::sys::fs::exists(file_path)) {
        return true;
    }
    
    return false;
}

bool TaskScheduler::isCppSourceFile(const std::string& file_path) {
    // 这个函数现在和shouldSkipFile逻辑一致，但保持为了兼容性
    return !shouldSkipFile(file_path);
}
