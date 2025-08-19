#include "interrupt_analyzer.h"
#include <iostream>
#include <chrono>
#include <algorithm>
#include <queue>
#include <unordered_map>

InterruptAnalyzer::InterruptAnalyzer(const std::string& compile_commands_path)
    : compile_db_path(compile_commands_path) {
    
    // 初始化缓存管理器
    cache_manager = std::make_unique<CacheManager>(&data);
    
    // 初始化前端管理器
    frontend_manager = std::make_unique<ClangFrontendManager>(&data, compile_db_path);
}

Json::Value InterruptAnalyzer::analyzeHandler(const std::string& handler_name, const std::string& handler_file) {
    std::cout << "\n🎯 开始分析中断处理函数: " << handler_name << std::endl;

    auto start_time = std::chrono::high_resolution_clock::now();

    // 加载并分析项目
    if (!loadAndAnalyzeProject()) {
        Json::Value error_result;
        error_result["error"] = "Failed to load and analyze project";
        return error_result;
    }

    Json::Value result;
    result["handler_name"] = handler_name;
    result["handler_file"] = handler_file;

    // 检查函数是否存在
    auto func_it = data.function_locations.find(handler_name);
    if (func_it == data.function_locations.end()) {
        result["error"] = "Handler function not found";
        return result;
    }

    // 构建可达函数集合
    std::unordered_set<std::string> reachable;
    std::unordered_set<std::string> indirect_calls;
    std::tie(reachable, indirect_calls) = buildReachableFunctions(handler_name);

    std::cout << "✅ 找到 " << reachable.size() << " 个可达函数";
    if (!indirect_calls.empty()) {
        std::cout << "，其中 " << indirect_calls.size() << " 个通过函数指针调用";
    }
    std::cout << std::endl;

    // 生成分析结果
    result = generateAnalysisResult(handler_name, handler_file, reachable, indirect_calls);

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    printAnalysisStatistics(duration.count());

    return result;
}

bool InterruptAnalyzer::loadAndAnalyzeProject() {
    // 检查是否存在缓存文件
    if (cache_manager->cacheExists()) {
        std::cout << "📦 发现分析缓存，正在加载..." << std::endl;
        if (cache_manager->loadFromCache()) {
            std::cout << "✅ 缓存加载成功" << std::endl;
            return true;
        } else {
            std::cout << "⚠️ 缓存加载失败，重新分析..." << std::endl;
        }
    }

    std::cout << "📁 开始分析项目源文件..." << std::endl;
    std::cout << "🔄 使用模块化多线程分析..." << std::endl;

    // 使用前端管理器运行分析
    bool success = frontend_manager->runAnalysis();
    
    if (success) {
        // 保存分析结果到缓存文件
        std::cout << "💾 保存分析结果到缓存..." << std::endl;
        if (cache_manager->saveToCache()) {
            std::cout << "✅ 缓存保存成功" << std::endl;
        } else {
            std::cout << "⚠️ 缓存保存失败" << std::endl;
        }
    }

    return success;
}

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
    std::cout << "✅ 模块化分析完成，用时 " << duration_ms << " ms" << std::endl;
    std::cout << "📊 统计信息:" << std::endl;
    std::cout << "   函数定义: " << data.function_locations.size() << std::endl;
    std::cout << "   全局变量: " << data.global_variables.size() << std::endl;
    std::cout << "   函数调用: " << data.all_calls.size() << std::endl;
    std::cout << "   函数指针调用: " << data.function_pointer_calls.size() << std::endl;
    std::cout << "   函数指针赋值: " << data.function_pointer_assignments.size() << std::endl;
    std::cout << "   全局变量写操作: " << data.all_writes.size() << std::endl;
    std::cout << "   寄存器操作: " << data.register_ops.size() << std::endl;
    std::cout << "   参数来源追踪: " << data.parameter_sources.size() << std::endl;
    std::cout << "   返回值追踪: " << data.return_values.size() << std::endl;
    std::cout << "   函数指针参数: " << data.fp_param_info.size() << std::endl;

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
