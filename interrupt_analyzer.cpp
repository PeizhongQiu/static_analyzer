#include "interrupt_analyzer.h"
#include "ast_visitor.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/ASTConsumers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/FileSystem.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <queue>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <jsoncpp/json/json.h>

using namespace clang;
using namespace clang::tooling;

//=============================================================================
// 编译数据库处理
//=============================================================================

/**
 * 编译命令条目
 */
struct LocalCompileCommand {
    std::string directory;
    std::string file;
    std::vector<std::string> arguments;
};

/**
 * 检查是否为 C/C++ 源文件
 */
bool isCppSourceFile(const std::string& file_path) {
    static const std::vector<std::string> cpp_extensions = {
        ".c", ".cc", ".cpp", ".cxx", ".c++", ".C"
    };

    std::string ext = llvm::sys::path::extension(file_path).str();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return std::find(cpp_extensions.begin(), cpp_extensions.end(), ext) != cpp_extensions.end();
}

/**
 * 加载 compile_commands.json
 */
bool loadCompileCommands(const std::string& db_path, std::vector<LocalCompileCommand>& commands) {
    if (!llvm::sys::fs::exists(db_path)) {
        std::cout << "❌ 找不到编译数据库: " << db_path << std::endl;
        return false;
    }

    std::ifstream file(db_path);
    if (!file.is_open()) {
        std::cout << "❌ 无法打开编译数据库" << std::endl;
        return false;
    }

    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(file, root) || !root.isArray()) {
        std::cout << "❌ 解析编译数据库失败" << std::endl;
        return false;
    }

    commands.clear();

    for (Json::ArrayIndex i = 0; i < root.size(); ++i) {
        const Json::Value& entry = root[i];
        if (!entry.isObject()) continue;

        LocalCompileCommand cmd;

        if (!entry.isMember("directory") || !entry.isMember("file")) continue;

        cmd.directory = entry["directory"].asString();
        cmd.file = entry["file"].asString();

        // 转换为绝对路径
        if (!llvm::sys::path::is_absolute(cmd.file)) {
            llvm::SmallString<256> absolute_path(cmd.directory);
            llvm::sys::path::append(absolute_path, cmd.file);
            cmd.file = absolute_path.str().str();
        }

        // 尝试规范化路径
        llvm::SmallString<256> canonical_path;
        if (llvm::sys::fs::real_path(cmd.file, canonical_path)) {
            continue; // 如果无法获取真实路径，跳过这个文件
        }
        cmd.file = canonical_path.str().str();

        if (!isCppSourceFile(cmd.file)) continue;

        // 读取编译参数
        if (entry.isMember("arguments") && entry["arguments"].isArray()) {
            for (const auto& arg : entry["arguments"]) {
                if (arg.isString()) {
                    cmd.arguments.push_back(arg.asString());
                }
            }
        } else if (entry.isMember("command") && entry["command"].isString()) {
            std::string command = entry["command"].asString();
            std::istringstream iss(command);
            std::string arg;
            while (iss >> std::quoted(arg)) {
                cmd.arguments.push_back(arg);
            }
        } else {
            continue;
        }

        commands.push_back(std::move(cmd));
    }

    std::cout << "✅ 加载了 " << commands.size() << " 个源文件的编译信息" << std::endl;
    return !commands.empty();
}

/**
 * 提取编译选项
 */
std::vector<std::string> extractCompileOptions(const std::vector<std::string>& args) {
    std::vector<std::string> options;
    std::unordered_set<std::string> seen_options;  // 用于去重

    for (size_t i = 1; i < args.size(); ++i) {  // 跳过编译器名称
        const std::string& arg = args[i];

        // 跳过源文件和输出文件
        if (isCppSourceFile(arg) || arg == "-o") {
            if (arg == "-o" && i + 1 < args.size()) {
                ++i;  // 跳过输出文件名
            }
            continue;
        }

        // 跳过可能导致冲突的选项
        if (arg == "--no-warn" || arg == "-w" || arg.substr(0, 8) == "-Wno-all" ||
            arg == "--help" || arg == "-h" || arg == "--version" || 
            arg.substr(0, 9) == "--target=" || arg.substr(0, 8) == "-target=" ||
            arg.substr(0, 7) == "-march=" || arg.substr(0, 7) == "-mcpu=" ||
            arg == "-v" || arg == "--verbose") {
            continue;
        }

        // 包含有用的编译选项，但要去重
        if (arg.substr(0, 2) == "-I" || arg.substr(0, 2) == "-D" || 
            arg.substr(0, 2) == "-f" || arg.substr(0, 2) == "-W" || 
            arg.substr(0, 4) == "-std" || arg == "-g" ||
            arg.substr(0, 2) == "-m" || arg.substr(0, 2) == "-O") {
            
            // 对于某些选项，只保留最后一个
            if (arg.substr(0, 4) == "-std" || arg.substr(0, 2) == "-O") {
                // 移除之前相同类型的选项
                for (auto it = options.begin(); it != options.end();) {
                    if ((arg.substr(0, 4) == "-std" && it->substr(0, 4) == "-std") ||
                        (arg.substr(0, 2) == "-O" && it->substr(0, 2) == "-O")) {
                        it = options.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            
            // 添加选项，如果之前没有见过
            if (seen_options.find(arg) == seen_options.end()) {
                options.push_back(arg);
                seen_options.insert(arg);
            }
        }
    }

    return options;
}

//=============================================================================
// Clang 前端类
//=============================================================================

class InterruptAnalysisConsumer : public ASTConsumer {
private:
    InterruptAnalysisVisitor Visitor;

public:
    explicit InterruptAnalysisConsumer(ASTContext* context, AnalysisData* data, const std::string& file)
        : Visitor(context, data, file) {}

    void HandleTranslationUnit(ASTContext& context) override {
        Visitor.TraverseDecl(context.getTranslationUnitDecl());
    }
};

class InterruptAnalysisAction : public ASTFrontendAction {
private:
    AnalysisData* Data;
    std::string CurrentFile;

public:
    explicit InterruptAnalysisAction(AnalysisData* data) : Data(data) {}

    std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override {
        CurrentFile = file.str();
        return std::make_unique<InterruptAnalysisConsumer>(&CI.getASTContext(), Data, CurrentFile);
    }
};

class InterruptAnalysisActionFactory : public FrontendActionFactory {
private:
    AnalysisData* Data;

public:
    explicit InterruptAnalysisActionFactory(AnalysisData* data) : Data(data) {}

    std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<InterruptAnalysisAction>(Data);
    }
};

//=============================================================================
// 主分析器实现
//=============================================================================

InterruptAnalyzer::InterruptAnalyzer(const std::string& compile_commands_path)
    : compile_db_path(compile_commands_path) {}

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
    // 生成缓存文件名
    std::string cache_file = "analysis_cache.json";
    
    // 检查是否存在缓存文件
    if (llvm::sys::fs::exists(cache_file)) {
        std::cout << "📦 发现分析缓存，正在加载..." << std::endl;
        if (loadFromCache(cache_file)) {
            std::cout << "✅ 缓存加载成功" << std::endl;
            return true;
        } else {
            std::cout << "⚠️ 缓存加载失败，重新分析..." << std::endl;
        }
    }

    // 加载编译数据库
    std::vector<LocalCompileCommand> compile_commands;
    if (!loadCompileCommands(compile_db_path, compile_commands)) {
        return false;
    }

    // 收集所有源文件和编译选项
    std::vector<std::string> source_files;
    std::unordered_map<std::string, int> arg_count;

    for (const auto& cmd : compile_commands) {
        source_files.push_back(cmd.file);

        auto compile_opts = extractCompileOptions(cmd.arguments);
        for (const auto& opt : compile_opts) {
            arg_count[opt]++;
        }
    }

    // 选择通用编译选项
    std::vector<std::string> common_args;
    int threshold = std::max(1, (int)(compile_commands.size() * 0.5));

    for (const auto& [arg, count] : arg_count) {
        if (count >= threshold) {
            common_args.push_back(arg);
        }
    }

    std::cout << "📁 准备分析 " << source_files.size() << " 个源文件" << std::endl;
    std::cout << "🔄 使用多线程进行分析..." << std::endl;

    // 直接创建编译数据库和工具，绕过 CommonOptionsParser
    std::string error_msg;
    auto compilation_db = CompilationDatabase::autoDetectFromDirectory(
        llvm::sys::path::parent_path(compile_db_path), error_msg);
    
    if (!compilation_db) {
        // 如果自动检测失败，尝试加载固定的编译数据库
        compilation_db = CompilationDatabase::loadFromDirectory(
            llvm::sys::path::parent_path(compile_db_path), error_msg);
    }
    
    if (!compilation_db) {
        std::cout << "❌ 无法加载编译数据库: " << error_msg << std::endl;
        return false;
    }

    // 分批处理源文件以支持多线程
    const size_t batch_size = 50; // 每批处理50个文件
    std::vector<std::vector<std::string>> file_batches;
    
    for (size_t i = 0; i < source_files.size(); i += batch_size) {
        std::vector<std::string> batch;
        for (size_t j = i; j < std::min(i + batch_size, source_files.size()); ++j) {
            batch.push_back(source_files[j]);
        }
        file_batches.push_back(batch);
    }

    std::cout << "📊 分成 " << file_batches.size() << " 批进行处理" << std::endl;

    // 串行处理每批文件（Clang工具本身会处理并行）
    for (size_t i = 0; i < file_batches.size(); ++i) {
        std::cout << "🔄 处理第 " << (i + 1) << "/" << file_batches.size() << " 批文件..." << std::endl;
        
        // 创建 Clang 工具
        ClangTool tool(*compilation_db, file_batches[i]);
        
        // 添加参数调整器来清理选项
        tool.appendArgumentsAdjuster([](const CommandLineArguments& args, llvm::StringRef) {
            CommandLineArguments adjusted_args;
            std::unordered_set<std::string> seen_options;
            
            for (const auto& arg : args) {
                // 跳过重复的 --no-warn 选项
                if (arg == "--no-warn" || arg == "-w") {
                    if (seen_options.find("no-warn") == seen_options.end()) {
                        seen_options.insert("no-warn");
                        adjusted_args.push_back("-w");  // 使用简短形式
                    }
                    continue;
                }
                
                // 跳过其他可能导致问题的选项
                if (arg.find("--help") == 0 || arg.find("--version") == 0 ||
                    arg.find("-march=") == 0 || arg.find("-mcpu=") == 0) {
                    continue;
                }
                
                adjusted_args.push_back(arg);
            }
            
            return adjusted_args;
        });

        // 运行分析
        InterruptAnalysisActionFactory factory(&data);
        int analysis_result = tool.run(&factory);

        if (analysis_result != 0) {
            std::cout << "⚠️ 第 " << (i + 1) << " 批文件分析时出现错误，继续处理..." << std::endl;
        }
    }

    // 保存分析结果到缓存文件
    std::cout << "💾 保存分析结果到缓存..." << std::endl;
    if (saveToCache(cache_file)) {
        std::cout << "✅ 缓存保存成功" << std::endl;
    } else {
        std::cout << "⚠️ 缓存保存失败" << std::endl;
    }

    return true;
}

bool InterruptAnalyzer::loadFromCache(const std::string& cache_file) {
    std::ifstream file(cache_file);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(file, root)) {
        return false;
    }

    // 清空当前数据，而不是重新赋值
    std::lock_guard<std::mutex> lock(data.mutex);
    
    data.function_locations.clear();
    data.function_signatures.clear();
    data.call_graph.clear();
    data.global_variables.clear();
    data.pointer_aliases.clear();
    data.function_pointer_calls.clear();
    data.function_pointer_assignments.clear();
    data.function_pointer_targets.clear();
    data.all_writes.clear();
    data.all_calls.clear();
    data.register_ops.clear();

    // 加载函数位置 (现在是 vector<string> 而不是单个位置)
    if (root.isMember("function_locations")) {
        for (const auto& item : root["function_locations"]) {
            if (!item.isMember("name") || !item.isMember("locations")) continue;
            
            std::string func_name = item["name"].asString();
            std::vector<std::string> locations;
            for (const auto& loc : item["locations"]) {
                locations.push_back(loc.asString());
            }
            data.function_locations[func_name] = locations;
        }
    }

    // 加载函数签名
    if (root.isMember("function_signatures")) {
        for (const auto& item : root["function_signatures"]) {
            if (!item.isMember("key") || !item.isMember("info")) continue;
            
            std::string key = item["key"].asString();
            const auto& info_json = item["info"];
            
            if (!info_json.isMember("name") || !info_json.isMember("file")) continue;
            
            FunctionInfo info;
            info.name = info_json["name"].asString();
            info.file = info_json["file"].asString();
            info.line = info_json.isMember("line") ? info_json["line"].asUInt() : 0;
            info.return_type = info_json.isMember("return_type") ? info_json["return_type"].asString() : "";
            info.is_static = info_json.isMember("is_static") ? info_json["is_static"].asBool() : false;
            info.linkage = info_json.isMember("linkage") ? info_json["linkage"].asString() : "";
            
            if (info_json.isMember("parameters")) {
                for (const auto& param : info_json["parameters"]) {
                    info.parameters.push_back(param.asString());
                }
            }
            data.function_signatures[key] = info;
        }
    }

    // 加载全局变量 (现在是 unordered_set<string>)
    if (root.isMember("global_variables")) {
        for (const auto& item : root["global_variables"]) {
	    if (!item.isMember("global_var") || !item.isMember("file")) continue;

            std::string global_var = item["global_var"].asString();
            std::string file = item["file"].asString();
            data.global_variables[global_var] = file;
        }
    }

    // 加载指针别名
    if (root.isMember("pointer_aliases")) {
        for (const auto& item : root["pointer_aliases"]) {
            if (!item.isMember("local_var") || !item.isMember("global_path")) continue;
            
            std::string local_var = item["local_var"].asString();
            std::string global_path = item["global_path"].asString();
            data.pointer_aliases[local_var] = global_path;
        }
    }

    // 加载函数调用图
    if (root.isMember("call_graph")) {
        for (const auto& item : root["call_graph"]) {
            if (!item.isMember("caller") || !item.isMember("callees")) continue;
            
            std::string caller = item["caller"].asString();
            for (const auto& callee : item["callees"]) {
                data.call_graph[caller].insert(callee.asString());
            }
        }
    }

    // 加载函数指针目标
    if (root.isMember("function_pointer_targets")) {
        for (const auto& item : root["function_pointer_targets"]) {
            if (!item.isMember("pointer_name") || !item.isMember("targets")) continue;
            
            std::string pointer_name = item["pointer_name"].asString();
            std::vector<std::string> targets;
            for (const auto& target : item["targets"]) {
                targets.push_back(target.asString());
            }
            data.function_pointer_targets[pointer_name] = targets;
        }
    }

    // 加载全局变量写操作
    if (root.isMember("all_writes")) {
        for (const auto& item : root["all_writes"]) {
            if (!item.isMember("target") || !item.isMember("file")) continue;
            
            WriteOperation write;
            write.target = item["target"].asString();
            write.node_id = item.isMember("node_id") ? item["node_id"].asString() : "";
            write.file = item["file"].asString();
            write.line = item.isMember("line") ? item["line"].asUInt() : 0;
            write.column = item.isMember("column") ? item["column"].asUInt() : 0;
            write.function = item.isMember("function") ? item["function"].asString() : "";
            write.ast_kind = item.isMember("ast_kind") ? item["ast_kind"].asString() : "";
            write.write_type = item.isMember("write_type") ? item["write_type"].asString() : "";
            data.all_writes.push_back(write);
        }
    }

    // 加载函数调用信息
    if (root.isMember("all_calls")) {
        for (const auto& item : root["all_calls"]) {
            if (!item.isMember("caller") || !item.isMember("callee")) continue;
            
            CallInfo call;
            call.caller = item["caller"].asString();
            call.callee = item["callee"].asString();
            call.node_id = item.isMember("node_id") ? item["node_id"].asString() : "";
            call.file = item.isMember("file") ? item["file"].asString() : "";
            call.line = item.isMember("line") ? item["line"].asUInt() : 0;
            call.column = item.isMember("column") ? item["column"].asUInt() : 0;
            call.is_indirect = item.isMember("is_indirect") ? item["is_indirect"].asBool() : false;
            data.all_calls.push_back(call);
        }
    }

    // 加载寄存器操作
    if (root.isMember("register_ops")) {
        for (const auto& item : root["register_ops"]) {
            if (!item.isMember("target") || !item.isMember("operation")) continue;
            
            RegisterOperation reg_op;
            reg_op.target = item["target"].asString();
            reg_op.operation = item["operation"].asString();
            reg_op.value = item.isMember("value") ? item["value"].asString() : "";
            reg_op.node_id = item.isMember("node_id") ? item["node_id"].asString() : "";
            reg_op.file = item.isMember("file") ? item["file"].asString() : "";
            reg_op.line = item.isMember("line") ? item["line"].asUInt() : 0;
            reg_op.column = item.isMember("column") ? item["column"].asUInt() : 0;
            reg_op.function = item.isMember("function") ? item["function"].asString() : "";
            reg_op.details = item.isMember("details") ? item["details"].asString() : "";
            data.register_ops.push_back(reg_op);
        }
    }

    // 加载函数指针调用
    if (root.isMember("function_pointer_calls")) {
        for (const auto& item : root["function_pointer_calls"]) {
            if (!item.isMember("caller") || !item.isMember("pointer_name")) continue;
            
            FunctionPointerCall fp_call;
            fp_call.caller = item["caller"].asString();
            fp_call.pointer_name = item["pointer_name"].asString();
            fp_call.pointer_type = item.isMember("pointer_type") ? item["pointer_type"].asString() : "";
            fp_call.node_id = item.isMember("node_id") ? item["node_id"].asString() : "";
            fp_call.file = item.isMember("file") ? item["file"].asString() : "";
            fp_call.line = item.isMember("line") ? item["line"].asUInt() : 0;
            fp_call.column = item.isMember("column") ? item["column"].asUInt() : 0;
            
            if (item.isMember("possible_targets")) {
                for (const auto& target : item["possible_targets"]) {
                    fp_call.possible_targets.push_back(target.asString());
                }
            }
            data.function_pointer_calls.push_back(fp_call);
        }
    }

    // 加载函数指针赋值
    if (root.isMember("function_pointer_assignments")) {
        for (const auto& item : root["function_pointer_assignments"]) {
            if (!item.isMember("pointer_name") || !item.isMember("target_function")) continue;
            
            FunctionPointerAssignment fp_assign;
            fp_assign.pointer_name = item["pointer_name"].asString();
            fp_assign.target_function = item["target_function"].asString();
            fp_assign.assignment_type = item.isMember("assignment_type") ? item["assignment_type"].asString() : "";
            fp_assign.file = item.isMember("file") ? item["file"].asString() : "";
            fp_assign.line = item.isMember("line") ? item["line"].asUInt() : 0;
            fp_assign.column = item.isMember("column") ? item["column"].asUInt() : 0;
            data.function_pointer_assignments.push_back(fp_assign);
        }
    }

    return true;
}

bool InterruptAnalyzer::saveToCache(const std::string& cache_file) {
    Json::Value root;

    // 保存函数位置 (现在是 vector<string>)
    Json::Value func_locations(Json::arrayValue);
    for (const auto& [name, locations] : data.function_locations) {
        Json::Value item;
        item["name"] = name;
        Json::Value locs(Json::arrayValue);
        for (const auto& loc : locations) {
            locs.append(loc);
        }
        item["locations"] = locs;
        func_locations.append(item);
    }
    root["function_locations"] = func_locations;

    // 保存函数签名
    Json::Value func_signatures(Json::arrayValue);
    for (const auto& [key, info] : data.function_signatures) {
        Json::Value item;
        item["key"] = key;
        Json::Value info_json;
        info_json["name"] = info.name;
        info_json["file"] = info.file;
        info_json["line"] = info.line;
        info_json["return_type"] = info.return_type;
        info_json["is_static"] = info.is_static;
        info_json["linkage"] = info.linkage;
        Json::Value params(Json::arrayValue);
        for (const auto& param : info.parameters) {
            params.append(param);
        }
        info_json["parameters"] = params;
        item["info"] = info_json;
        func_signatures.append(item);
    }
    root["function_signatures"] = func_signatures;

    // 保存全局变量 (现在是 unordered_set<string>)
    Json::Value global_vars(Json::arrayValue);
    for (const auto& [global_var, file] : data.global_variables) {
	Json::Value item;
        item["global_var"] = global_var;
        item["file"] = file;
        global_vars.append(item);
    }
    root["global_variables"] = global_vars;

    // 保存指针别名
    Json::Value pointer_aliases(Json::arrayValue);
    for (const auto& [local_var, global_path] : data.pointer_aliases) {
        Json::Value item;
        item["local_var"] = local_var;
        item["global_path"] = global_path;
        pointer_aliases.append(item);
    }
    root["pointer_aliases"] = pointer_aliases;

    // 保存调用图
    Json::Value call_graph(Json::arrayValue);
    for (const auto& [caller, callees] : data.call_graph) {
        Json::Value item;
        item["caller"] = caller;
        Json::Value callees_array(Json::arrayValue);
        for (const auto& callee : callees) {
            callees_array.append(callee);
        }
        item["callees"] = callees_array;
        call_graph.append(item);
    }
    root["call_graph"] = call_graph;

    // 保存函数指针目标
    Json::Value fp_targets(Json::arrayValue);
    for (const auto& [pointer_name, targets] : data.function_pointer_targets) {
        Json::Value item;
        item["pointer_name"] = pointer_name;
        Json::Value targets_array(Json::arrayValue);
        for (const auto& target : targets) {
            targets_array.append(target);
        }
        item["targets"] = targets_array;
        fp_targets.append(item);
    }
    root["function_pointer_targets"] = fp_targets;

    // 保存写操作
    Json::Value writes(Json::arrayValue);
    for (const auto& write : data.all_writes) {
        Json::Value item;
        item["target"] = write.target;
        item["node_id"] = write.node_id;
        item["file"] = write.file;
        item["line"] = write.line;
        item["column"] = write.column;
        item["function"] = write.function;
        item["ast_kind"] = write.ast_kind;
        item["write_type"] = write.write_type;
        writes.append(item);
    }
    root["all_writes"] = writes;

    // 保存函数调用信息
    Json::Value calls(Json::arrayValue);
    for (const auto& call : data.all_calls) {
        Json::Value item;
        item["caller"] = call.caller;
        item["callee"] = call.callee;
        item["node_id"] = call.node_id;
        item["file"] = call.file;
        item["line"] = call.line;
        item["column"] = call.column;
        item["is_indirect"] = call.is_indirect;
        calls.append(item);
    }
    root["all_calls"] = calls;

    // 保存寄存器操作
    Json::Value reg_ops(Json::arrayValue);
    for (const auto& reg_op : data.register_ops) {
        Json::Value item;
        item["target"] = reg_op.target;
        item["operation"] = reg_op.operation;
        item["value"] = reg_op.value;
        item["node_id"] = reg_op.node_id;
        item["file"] = reg_op.file;
        item["line"] = reg_op.line;
        item["column"] = reg_op.column;
        item["function"] = reg_op.function;
        item["details"] = reg_op.details;
        reg_ops.append(item);
    }
    root["register_ops"] = reg_ops;

    // 保存函数指针调用
    Json::Value fp_calls(Json::arrayValue);
    for (const auto& fp_call : data.function_pointer_calls) {
        Json::Value item;
        item["caller"] = fp_call.caller;
        item["pointer_name"] = fp_call.pointer_name;
        item["pointer_type"] = fp_call.pointer_type;
        item["node_id"] = fp_call.node_id;
        item["file"] = fp_call.file;
        item["line"] = fp_call.line;
        item["column"] = fp_call.column;
        Json::Value targets(Json::arrayValue);
        for (const auto& target : fp_call.possible_targets) {
            targets.append(target);
        }
        item["possible_targets"] = targets;
        fp_calls.append(item);
    }
    root["function_pointer_calls"] = fp_calls;

    // 保存函数指针赋值
    Json::Value fp_assigns(Json::arrayValue);
    for (const auto& fp_assign : data.function_pointer_assignments) {
        Json::Value item;
        item["pointer_name"] = fp_assign.pointer_name;
        item["target_function"] = fp_assign.target_function;
        item["assignment_type"] = fp_assign.assignment_type;
        item["file"] = fp_assign.file;
        item["line"] = fp_assign.line;
        item["column"] = fp_assign.column;
        fp_assigns.append(item);
    }
    root["function_pointer_assignments"] = fp_assigns;

    // 写入文件
    std::ofstream file(cache_file);
    if (!file.is_open()) {
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);

    return true;
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
    std::cout << "✅ 分析完成，用时 " << duration_ms << " ms" << std::endl;
    std::cout << "📊 统计信息:" << std::endl;
    std::cout << "   函数定义: " << data.function_locations.size() << std::endl;
    std::cout << "   全局变量: " << data.global_variables.size() << std::endl;
    std::cout << "   函数调用: " << data.all_calls.size() << std::endl;
    std::cout << "   函数指针调用: " << data.function_pointer_calls.size() << std::endl;
    std::cout << "   函数指针赋值: " << data.function_pointer_assignments.size() << std::endl;
    std::cout << "   全局变量写操作: " << data.all_writes.size() << std::endl;
    std::cout << "   寄存器操作: " << data.register_ops.size() << std::endl;

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
}
