// interrupt_analyzer.cpp - 流式处理版本实现
#include "interrupt_analyzer.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <iomanip>
#include <queue>

InterruptAnalyzer::InterruptAnalyzer(const std::string& compile_commands_path, 
                                   const StreamingConfig& cfg)
    : compile_db_path(compile_commands_path), config(cfg) {
    
    // 初始化原有组件
    cache_manager = std::make_unique<CacheManager>(&data);
    frontend_manager = std::make_unique<ClangFrontendManager>(&data, compile_db_path);
    
    std::cout << "🚀 流式中断分析器已初始化" << std::endl;
    std::cout << "   工作线程数: " << config.max_worker_threads << std::endl;
    std::cout << "   最大内存: " << config.max_memory_mb << " MB" << std::endl;
    std::cout << "   批处理大小: " << config.batch_size << std::endl;
    std::cout << "   增量分析: " << (config.enable_incremental ? "启用" : "禁用") << std::endl;
}

InterruptAnalyzer::~InterruptAnalyzer() {
    cleanupStreamingComponents();
}

Json::Value InterruptAnalyzer::analyzeHandlerStreaming(const std::string& handler_name,
                                                     const std::string& handler_file) {
    std::cout << "\n🚀 开始流式分析中断处理函数: " << handler_name << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();

    // 使用流式处理方式分析项目
    if (!loadAndAnalyzeProjectStreaming()) {
        Json::Value error_result;
        error_result["error"] = "Failed to load and analyze project with streaming";
        return error_result;
    }

    Json::Value result;
    result["handler_name"] = handler_name;
    result["handler_file"] = handler_file;

    auto func_it = data.function_locations.find(handler_name);
    if (func_it == data.function_locations.end()) {
        result["error"] = "Handler function not found";
        return result;
    }

    std::unordered_set<std::string> reachable;
    std::unordered_set<std::string> indirect_calls;
    std::tie(reachable, indirect_calls) = buildReachableFunctions(handler_name);

    std::cout << "✅ 找到 " << reachable.size() << " 个可达函数";
    if (!indirect_calls.empty()) {
        std::cout << "，其中 " << indirect_calls.size() << " 个通过函数指针调用";
    }
    std::cout << std::endl;

    result = generateAnalysisResult(handler_name, handler_file, reachable, indirect_calls);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // 打印流式处理统计信息
    printStreamingStatistics();
    
    // 添加流式处理统计到结果中
    result["streaming_stats"] = Json::Value();
    auto stats = getStatistics();
    result["streaming_stats"]["total_files"] = static_cast<int>(stats.total_files);
    result["streaming_stats"]["cache_hit_rate"] = static_cast<int>(stats.cache_hit_rate_percent);
    result["streaming_stats"]["throughput"] = stats.throughput_files_per_second;
    result["streaming_stats"]["memory_usage_mb"] = static_cast<int>(stats.memory_usage_mb);
    result["streaming_stats"]["analysis_time"] = stats.analysis_time_seconds;

    return result;
}

void InterruptAnalyzer::updateConfig(const StreamingConfig& new_config) {
    config = new_config;
    std::cout << "📝 流式处理配置已更新" << std::endl;
}

bool InterruptAnalyzer::loadAndAnalyzeProjectStreaming() {
    // 检查缓存
    if (cache_manager->cacheExists()) {
        std::cout << "📦 发现分析缓存，正在加载..." << std::endl;
        if (cache_manager->loadFromCache()) {
            std::cout << "✅ 缓存加载成功" << std::endl;
            return true;
        } else {
            std::cout << "⚠️ 缓存加载失败，进行流式重新分析..." << std::endl;
        }
    }

    std::cout << "📁 开始流式分析项目源文件..." << std::endl;
    std::cout << "🔄 使用 " << config.max_worker_threads << " 个工作线程..." << std::endl;

    // 初始化流式处理组件
    initializeStreamingComponents();

    // 加载编译数据库
    CompilationDatabaseProcessor db_processor(compile_db_path);
    if (!db_processor.loadCompileCommands()) {
        std::cout << "❌ 无法加载编译数据库" << std::endl;
        return false;
    }

    // 直接从编译数据库获取源文件，而不是从命令行
    std::vector<std::string> source_files;
    const auto& commands = db_processor.getCommands();
    
    for (const auto& cmd : commands) {
        // 构建完整的文件路径
        std::string file_path = cmd.file;
        
        // 如果是相对路径，相对于编译目录解析
        if (!llvm::sys::path::is_absolute(file_path)) {
            file_path = cmd.directory + "/" + file_path;
        }
        
        // 规范化路径
        llvm::SmallString<256> normalized_path(file_path);
        llvm::sys::path::remove_dots(normalized_path, true);
        
        source_files.push_back(normalized_path.str().str());
    }
    
    total_files = source_files.size();
    
    std::cout << "📊 从编译数据库获取 " << total_files << " 个源文件" << std::endl;

    // 创建流式任务
    auto tasks = TaskScheduler::createTasks(source_files);
    
    // 配置并启动流式处理器
    stream_processor->setCompileDbPath(compile_db_path);
    stream_processor->setMaxMemoryUsage(config.max_memory_mb * 1024 * 1024);
    stream_processor->start();
    
    // 添加所有任务
    stream_processor->addTasks(tasks);

    // 等待所有任务完成
    auto start_time = std::chrono::high_resolution_clock::now();
    while (stream_processor->getProcessedFiles() < tasks.size()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // 更新统计信息
        processed_files = stream_processor->getProcessedFiles();
        cache_hits = stream_processor->getProcessedFiles() * stream_processor->getCacheHitRate() / 100;
        error_count = stream_processor->getErrorCount();
    }

    // 停止流式处理器
    stream_processor->stop();
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "✅ 流式分析完成，耗时 " << duration.count() << " ms" << std::endl;

    // 保存缓存
    if (cache_manager->saveToCache()) {
        std::cout << "💾 缓存保存成功" << std::endl;
    }

    return true;
}

void InterruptAnalyzer::initializeStreamingComponents() {
    // 创建流式处理器
    stream_processor = std::make_unique<StreamProcessor>(config.max_worker_threads);

    // 创建结果聚合器并将所有权转移给流式处理器
    auto result_aggregator = std::make_unique<FileResultAggregator>(
        &data,
        config.batch_size,
        config.max_memory_mb * 1024 * 1024 / 4  // 使用1/4内存作为聚合缓冲
    );

    // 设置回调并转移所有权，避免双重释放
    stream_processor->setCallback(std::move(result_aggregator));
}

void InterruptAnalyzer::cleanupStreamingComponents() {
    if (stream_processor) {
        stream_processor->stop();
        stream_processor.reset();
    }
}

InterruptAnalyzer::AnalysisStats InterruptAnalyzer::getStatistics() const {
    AnalysisStats stats;
    stats.total_files = total_files;
    stats.processed_files = processed_files;
    stats.cache_hits = cache_hits;
    stats.errors = error_count;
    
    if (stream_processor) {
        stats.memory_usage_mb = stream_processor->getCurrentMemoryUsage() / (1024 * 1024);
    }
    
    if (processed_files > 0) {
        stats.cache_hit_rate_percent = (cache_hits * 100) / processed_files;
    }
    
    // 计算吞吐量（需要记录时间）
    // 这里简化处理，实际应该记录开始和结束时间
    stats.throughput_files_per_second = 0.0;
    stats.analysis_time_seconds = 0.0;
    
    return stats;
}

void InterruptAnalyzer::printStreamingStatistics() {
    auto stats = getStatistics();
    
    std::cout << "\n📈 流式处理统计信息:" << std::endl;
    std::cout << "   总文件数: " << stats.total_files << std::endl;
    std::cout << "   处理成功: " << stats.processed_files << std::endl;
    std::cout << "   缓存命中: " << stats.cache_hits << " (" << stats.cache_hit_rate_percent << "%)" << std::endl;
    std::cout << "   处理错误: " << stats.errors << std::endl;
    std::cout << "   内存使用: " << stats.memory_usage_mb << " MB" << std::endl;
    
    // 打印分析数据统计
    std::cout << "\n📊 分析数据统计:" << std::endl;
    std::cout << "   函数定义: " << data.function_locations.size() << std::endl;
    std::cout << "   全局变量: " << data.global_variables.size() << std::endl;
    std::cout << "   函数调用: " << data.all_calls.size() << std::endl;
    std::cout << "   函数指针调用: " << data.function_pointer_calls.size() << std::endl;
    std::cout << "   全局变量写操作: " << data.all_writes.size() << std::endl;
    std::cout << "   寄存器操作: " << data.register_ops.size() << std::endl;
    std::cout << "   参数来源追踪: " << data.parameter_sources.size() << std::endl;
    std::cout << "   返回值追踪: " << data.return_values.size() << std::endl;
    
    // 显示写操作分类统计
    std::unordered_map<std::string, int> write_type_counts;
    for (const auto& write : data.all_writes) {
        write_type_counts[write.write_type]++;
    }
    
    if (!write_type_counts.empty()) {
        std::cout << "   写操作分类:" << std::endl;
        for (const auto& [type, count] : write_type_counts) {
            std::cout << "     " << type << ": " << count << std::endl;
        }
    }
    
    // 显示回溯成功的写操作数量
    int resolved_writes = 0;
    for (const auto& write : data.all_writes) {
        if (write.target.find("->") != std::string::npos || 
            write.target.find(".") != std::string::npos) {
            resolved_writes++;
        }
    }
    std::cout << "   成功回溯的写操作: " << resolved_writes << std::endl;
}

// 保持原有方法不变
std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>>
InterruptAnalyzer::buildReachableFunctions(const std::string& handler_name) {
    std::unordered_set<std::string> reachable;
    std::unordered_set<std::string> indirect_calls;
    std::queue<std::string> queue;

    queue.push(handler_name);
    reachable.insert(handler_name);

    while (!queue.empty()) {
        std::string current = queue.front();
        queue.pop();

        // 处理直接函数调用
        auto call_it = data.call_graph.find(current);
        if (call_it != data.call_graph.end()) {
            for (const auto& callee : call_it->second) {
                if (reachable.find(callee) == reachable.end()) {
                    reachable.insert(callee);
                    queue.push(callee);
                }
            }
        }

        // 处理函数指针调用
        for (const auto& fp_call : data.function_pointer_calls) {
            if (fp_call.caller == current) {
                for (const auto& target : fp_call.possible_targets) {
                    if (reachable.find(target) == reachable.end()) {
                        reachable.insert(target);
                        queue.push(target);
                        indirect_calls.insert(target);
                    }
                }
            }
        }
    }

    return {reachable, indirect_calls};
}

Json::Value InterruptAnalyzer::generateAnalysisResult(const std::string& handler_name,
                                                     const std::string& handler_file,
                                                     const std::unordered_set<std::string>& reachable,
                                                     const std::unordered_set<std::string>& indirect_calls) {
    Json::Value result;
    result["handler_name"] = handler_name;
    result["handler_file"] = handler_file;

    // 可达函数列表
    Json::Value reachable_funcs(Json::arrayValue);
    for (const auto& func : reachable) {
        reachable_funcs.append(func);
    }
    result["reachable_functions"] = reachable_funcs;

    // 间接调用函数列表
    Json::Value indirect_funcs(Json::arrayValue);
    for (const auto& func : indirect_calls) {
        indirect_funcs.append(func);
    }
    result["indirect_call_functions"] = indirect_funcs;

    // 过滤各种操作
    result["filtered_writes"] = filterWrites(reachable);
    result["register_operations"] = filterRegisterOperations(reachable);
    result["function_pointer_calls"] = filterFunctionPointerCalls(reachable);
    result["function_pointer_assignments"] = generateFunctionPointerAssignments();

    // 统计信息
    result["statistics"] = generateStatistics(reachable, indirect_calls, result);

    return result;
}

Json::Value InterruptAnalyzer::filterWrites(const std::unordered_set<std::string>& reachable) {
    Json::Value filtered_writes(Json::arrayValue);
    for (const auto& write : data.all_writes) {
        if (reachable.find(write.function) != reachable.end()) {
            Json::Value write_json;
            write_json["function"] = write.function;
            write_json["file"] = write.file;
            write_json["line"] = write.line;
            write_json["write_type"] = write.write_type;
            write_json["target"] = write.target;
            filtered_writes.append(write_json);
        }
    }
    return filtered_writes;
}

Json::Value InterruptAnalyzer::filterRegisterOperations(const std::unordered_set<std::string>& reachable) {
    Json::Value register_operations(Json::arrayValue);
    for (const auto& reg_op : data.register_ops) {
        if (reachable.find(reg_op.function) != reachable.end()) {
            Json::Value reg_json;
            reg_json["target"] = reg_op.target;
            reg_json["operation"] = reg_op.operation;
            reg_json["function"] = reg_op.function;
            reg_json["file"] = reg_op.file;
            reg_json["line"] = reg_op.line;
            reg_json["details"] = reg_op.details;
            register_operations.append(reg_json);
        }
    }
    return register_operations;
}

Json::Value InterruptAnalyzer::filterFunctionPointerCalls(const std::unordered_set<std::string>& reachable) {
    Json::Value function_pointer_calls(Json::arrayValue);
    for (const auto& fp_call : data.function_pointer_calls) {
        if (reachable.find(fp_call.caller) != reachable.end()) {
            Json::Value fp_json;
            fp_json["caller"] = fp_call.caller;
            fp_json["pointer_name"] = fp_call.pointer_name;
            fp_json["pointer_type"] = fp_call.pointer_type;
            fp_json["file"] = fp_call.file;
            fp_json["line"] = fp_call.line;

            Json::Value targets(Json::arrayValue);
            for (const auto& target : fp_call.possible_targets) {
                targets.append(target);
            }
            fp_json["possible_targets"] = targets;

            function_pointer_calls.append(fp_json);
        }
    }
    return function_pointer_calls;
}

Json::Value InterruptAnalyzer::generateFunctionPointerAssignments() {
    Json::Value function_pointer_assignments(Json::arrayValue);
    for (const auto& fp_assign : data.function_pointer_assignments) {
        Json::Value assign_json;
        assign_json["pointer_name"] = fp_assign.pointer_name;
        assign_json["target_function"] = fp_assign.target_function;
        assign_json["assignment_type"] = fp_assign.assignment_type;
        assign_json["file"] = fp_assign.file;
        assign_json["line"] = fp_assign.line;
        function_pointer_assignments.append(assign_json);
    }
    return function_pointer_assignments;
}

Json::Value InterruptAnalyzer::generateStatistics(const std::unordered_set<std::string>& reachable,
                                                 const std::unordered_set<std::string>& indirect_calls,
                                                 const Json::Value& result) {
    Json::Value stats;
    stats["reachable_functions"] = static_cast<int>(reachable.size());
    stats["indirect_call_functions"] = static_cast<int>(indirect_calls.size());
    stats["total_writes"] = static_cast<int>(result["filtered_writes"].size());
    stats["register_operations"] = static_cast<int>(result["register_operations"].size());
    stats["function_pointer_calls"] = static_cast<int>(result["function_pointer_calls"].size());
    stats["function_pointer_assignments"] = static_cast<int>(result["function_pointer_assignments"].size());
    return stats;
}

void InterruptAnalyzer::printAnalysisStatistics(long duration_ms) {
    std::cout << "✅ 分析完成，用时 " << duration_ms << " ms" << std::endl;
    std::cout << "📊 统计信息:" << std::endl;
    std::cout << "   函数定义: " << data.function_locations.size() << std::endl;
    std::cout << "   全局变量: " << data.global_variables.size() << std::endl;
    std::cout << "   函数调用: " << data.all_calls.size() << std::endl;
    std::cout << "   函数指针调用: " << data.function_pointer_calls.size() << std::endl;
    std::cout << "   函数指针赋值: " << data.function_pointer_assignments.size() << std::endl;
    std::cout << "   全局变量写操作: " << data.all_writes.size() << std::endl;
    std::cout << "   寄存器操作: " << data.register_ops.size() << std::endl;
}
