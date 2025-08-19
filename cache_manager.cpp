#include "cache_manager.h"
#include <llvm/Support/FileSystem.h>
#include <fstream>
#include <iostream>

CacheManager::CacheManager(AnalysisData* analysis_data, const std::string& cache_file)
    : data(analysis_data), cache_file_path(cache_file) {}

//=============================================================================
// 缓存操作
//=============================================================================

bool CacheManager::loadFromCache() {
    std::ifstream file(cache_file_path);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(file, root)) {
        return false;
    }

    // 清空当前数据
    clearAllData();

    // 反序列化各种数据
    bool success = true;
    success &= deserializeFunctionLocations(root);
    success &= deserializeFunctionSignatures(root);
    success &= deserializeGlobalVariables(root);
    success &= deserializePointerAliases(root);
    success &= deserializeCallGraph(root);
    success &= deserializeFunctionPointerTargets(root);
    success &= deserializeWrites(root);
    success &= deserializeCalls(root);
    success &= deserializeRegisterOps(root);
    success &= deserializeFunctionPointerCalls(root);
    success &= deserializeFunctionPointerAssignments(root);
    success &= deserializeParameterSources(root);
    success &= deserializeReturnValues(root);
    success &= deserializeFunctionPointerParams(root);

    return success;
}

bool CacheManager::saveToCache() {
    Json::Value root;

    // 序列化各种数据
    root["function_locations"] = serializeFunctionLocations();
    root["function_signatures"] = serializeFunctionSignatures();
    root["global_variables"] = serializeGlobalVariables();
    root["pointer_aliases"] = serializePointerAliases();
    root["call_graph"] = serializeCallGraph();
    root["function_pointer_targets"] = serializeFunctionPointerTargets();
    root["all_writes"] = serializeWrites();
    root["all_calls"] = serializeCalls();
    root["register_ops"] = serializeRegisterOps();
    root["function_pointer_calls"] = serializeFunctionPointerCalls();
    root["function_pointer_assignments"] = serializeFunctionPointerAssignments();
    root["parameter_sources"] = serializeParameterSources();
    root["return_values"] = serializeReturnValues();
    root["fp_param_info"] = serializeFunctionPointerParams();

    // 写入文件
    std::ofstream file(cache_file_path);
    if (!file.is_open()) {
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);

    return true;
}

bool CacheManager::cacheExists() const {
    return llvm::sys::fs::exists(cache_file_path);
}

void CacheManager::clearCache() {
    llvm::sys::fs::remove(cache_file_path);
}

//=============================================================================
// JSON序列化
//=============================================================================

Json::Value CacheManager::serializeFunctionLocations() {
    Json::Value func_locations(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [name, locations] : data->function_locations) {
        Json::Value item;
        item["name"] = name;
        Json::Value locs(Json::arrayValue);
        for (const auto& loc : locations) {
            locs.append(loc);
        }
        item["locations"] = locs;
        func_locations.append(item);
    }
    return func_locations;
}

Json::Value CacheManager::serializeFunctionSignatures() {
    Json::Value func_signatures(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [key, info] : data->function_signatures) {
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
    return func_signatures;
}

Json::Value CacheManager::serializeGlobalVariables() {
    Json::Value global_vars(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [global_var, file] : data->global_variables) {
        Json::Value item;
        item["global_var"] = global_var;
        item["file"] = file;
        global_vars.append(item);
    }
    return global_vars;
}

Json::Value CacheManager::serializePointerAliases() {
    Json::Value pointer_aliases(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [local_var, global_path] : data->pointer_aliases) {
        Json::Value item;
        item["local_var"] = local_var;
        item["global_path"] = global_path;
        pointer_aliases.append(item);
    }
    return pointer_aliases;
}

Json::Value CacheManager::serializeCallGraph() {
    Json::Value call_graph(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [caller, callees] : data->call_graph) {
        Json::Value item;
        item["caller"] = caller;
        Json::Value callees_array(Json::arrayValue);
        for (const auto& callee : callees) {
            callees_array.append(callee);
        }
        item["callees"] = callees_array;
        call_graph.append(item);
    }
    return call_graph;
}

Json::Value CacheManager::serializeFunctionPointerTargets() {
    Json::Value fp_targets(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& [pointer_name, targets] : data->function_pointer_targets) {
        Json::Value item;
        item["pointer_name"] = pointer_name;
        Json::Value targets_array(Json::arrayValue);
        for (const auto& target : targets) {
            targets_array.append(target);
        }
        item["targets"] = targets_array;
        fp_targets.append(item);
    }
    return fp_targets;
}

Json::Value CacheManager::serializeWrites() {
    Json::Value writes(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& write : data->all_writes) {
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
    return writes;
}

Json::Value CacheManager::serializeCalls() {
    Json::Value calls(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& call : data->all_calls) {
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
    return calls;
}

Json::Value CacheManager::serializeRegisterOps() {
    Json::Value reg_ops(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& reg_op : data->register_ops) {
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
    return reg_ops;
}

Json::Value CacheManager::serializeFunctionPointerCalls() {
    Json::Value fp_calls(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& fp_call : data->function_pointer_calls) {
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
    return fp_calls;
}

Json::Value CacheManager::serializeFunctionPointerAssignments() {
    Json::Value fp_assigns(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& fp_assign : data->function_pointer_assignments) {
        Json::Value item;
        item["pointer_name"] = fp_assign.pointer_name;
        item["target_function"] = fp_assign.target_function;
        item["assignment_type"] = fp_assign.assignment_type;
        item["file"] = fp_assign.file;
        item["line"] = fp_assign.line;
        item["column"] = fp_assign.column;
        fp_assigns.append(item);
    }
    return fp_assigns;
}

Json::Value CacheManager::serializeParameterSources() {
    Json::Value param_sources(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& param_source : data->parameter_sources) {
        Json::Value item;
        item["function_name"] = param_source.function_name;
        item["param_name"] = param_source.param_name;
        item["param_index"] = param_source.param_index;
        item["global_source"] = param_source.global_source;
        item["source_expression"] = param_source.source_expression;
        item["caller_function"] = param_source.caller_function;
        item["file"] = param_source.file;
        item["line"] = param_source.line;
        param_sources.append(item);
    }
    return param_sources;
}

Json::Value CacheManager::serializeReturnValues() {
    Json::Value return_vals(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& return_info : data->return_values) {
        Json::Value item;
        item["function_name"] = return_info.function_name;
        item["returned_global"] = return_info.returned_global;
        item["return_expression"] = return_info.return_expression;
        item["file"] = return_info.file;
        item["line"] = return_info.line;
        return_vals.append(item);
    }
    return return_vals;
}

Json::Value CacheManager::serializeFunctionPointerParams() {
    Json::Value fp_params(Json::arrayValue);
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& fp_param : data->fp_param_info) {
        Json::Value item;
        item["caller_function"] = fp_param.caller_function;
        item["fp_name"] = fp_param.fp_name;
        item["param_index"] = fp_param.param_index;
        item["target_function"] = fp_param.target_function;
        item["global_source"] = fp_param.global_source;
        item["file"] = fp_param.file;
        item["line"] = fp_param.line;
        fp_params.append(item);
    }
    return fp_params;
}

//=============================================================================
// JSON反序列化
//=============================================================================

bool CacheManager::deserializeFunctionLocations(const Json::Value& root) {
    if (!root.isMember("function_locations")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["function_locations"]) {
        if (!item.isMember("name") || !item.isMember("locations")) continue;
        
        std::string func_name = item["name"].asString();
        std::vector<std::string> locations;
        for (const auto& loc : item["locations"]) {
            locations.push_back(loc.asString());
        }
        data->function_locations[func_name] = locations;
    }
    return true;
}

bool CacheManager::deserializeFunctionSignatures(const Json::Value& root) {
    if (!root.isMember("function_signatures")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
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
        data->function_signatures[key] = info;
    }
    return true;
}

bool CacheManager::deserializeGlobalVariables(const Json::Value& root) {
    if (!root.isMember("global_variables")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["global_variables"]) {
        if (!item.isMember("global_var") || !item.isMember("file")) continue;

        std::string global_var = item["global_var"].asString();
        std::string file = item["file"].asString();
        data->global_variables[global_var] = file;
    }
    return true;
}

bool CacheManager::deserializePointerAliases(const Json::Value& root) {
    if (!root.isMember("pointer_aliases")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["pointer_aliases"]) {
        if (!item.isMember("local_var") || !item.isMember("global_path")) continue;
        
        std::string local_var = item["local_var"].asString();
        std::string global_path = item["global_path"].asString();
        data->pointer_aliases[local_var] = global_path;
    }
    return true;
}

bool CacheManager::deserializeCallGraph(const Json::Value& root) {
    if (!root.isMember("call_graph")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["call_graph"]) {
        if (!item.isMember("caller") || !item.isMember("callees")) continue;
        
        std::string caller = item["caller"].asString();
        for (const auto& callee : item["callees"]) {
            data->call_graph[caller].insert(callee.asString());
        }
    }
    return true;
}

bool CacheManager::deserializeFunctionPointerTargets(const Json::Value& root) {
    if (!root.isMember("function_pointer_targets")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["function_pointer_targets"]) {
        if (!item.isMember("pointer_name") || !item.isMember("targets")) continue;
        
        std::string pointer_name = item["pointer_name"].asString();
        std::vector<std::string> targets;
        for (const auto& target : item["targets"]) {
            targets.push_back(target.asString());
        }
        data->function_pointer_targets[pointer_name] = targets;
    }
    return true;
}

bool CacheManager::deserializeWrites(const Json::Value& root) {
    if (!root.isMember("all_writes")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
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
        data->all_writes.push_back(write);
    }
    return true;
}

bool CacheManager::deserializeCalls(const Json::Value& root) {
    if (!root.isMember("all_calls")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
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
        data->all_calls.push_back(call);
    }
    return true;
}

bool CacheManager::deserializeRegisterOps(const Json::Value& root) {
    if (!root.isMember("register_ops")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
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
        data->register_ops.push_back(reg_op);
    }
    return true;
}

bool CacheManager::deserializeFunctionPointerCalls(const Json::Value& root) {
    if (!root.isMember("function_pointer_calls")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
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
        data->function_pointer_calls.push_back(fp_call);
    }
    return true;
}

bool CacheManager::deserializeFunctionPointerAssignments(const Json::Value& root) {
    if (!root.isMember("function_pointer_assignments")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["function_pointer_assignments"]) {
        if (!item.isMember("pointer_name") || !item.isMember("target_function")) continue;
        
        FunctionPointerAssignment fp_assign;
        fp_assign.pointer_name = item["pointer_name"].asString();
        fp_assign.target_function = item["target_function"].asString();
        fp_assign.assignment_type = item.isMember("assignment_type") ? item["assignment_type"].asString() : "";
        fp_assign.file = item.isMember("file") ? item["file"].asString() : "";
        fp_assign.line = item.isMember("line") ? item["line"].asUInt() : 0;
        fp_assign.column = item.isMember("column") ? item["column"].asUInt() : 0;
        data->function_pointer_assignments.push_back(fp_assign);
    }
    return true;
}

bool CacheManager::deserializeParameterSources(const Json::Value& root) {
    if (!root.isMember("parameter_sources")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["parameter_sources"]) {
        if (!item.isMember("function_name") || !item.isMember("param_name")) continue;
        
        ParameterSource param_source;
        param_source.function_name = item["function_name"].asString();
        param_source.param_name = item["param_name"].asString();
        param_source.param_index = item.isMember("param_index") ? item["param_index"].asUInt() : 0;
        param_source.global_source = item.isMember("global_source") ? item["global_source"].asString() : "";
        param_source.source_expression = item.isMember("source_expression") ? item["source_expression"].asString() : "";
        param_source.caller_function = item.isMember("caller_function") ? item["caller_function"].asString() : "";
        param_source.file = item.isMember("file") ? item["file"].asString() : "";
        param_source.line = item.isMember("line") ? item["line"].asUInt() : 0;
        data->parameter_sources.push_back(param_source);
        
        // 同时更新 param_to_globals 映射
        std::string key = param_source.function_name + "::" + param_source.param_name;
        data->param_to_globals[key].push_back(param_source.global_source);
    }
    return true;
}

bool CacheManager::deserializeReturnValues(const Json::Value& root) {
    if (!root.isMember("return_values")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["return_values"]) {
        if (!item.isMember("function_name") || !item.isMember("returned_global")) continue;
        
        ReturnValueInfo return_info;
        return_info.function_name = item["function_name"].asString();
        return_info.returned_global = item["returned_global"].asString();
        return_info.return_expression = item.isMember("return_expression") ? item["return_expression"].asString() : "";
        return_info.file = item.isMember("file") ? item["file"].asString() : "";
        return_info.line = item.isMember("line") ? item["line"].asUInt() : 0;
        data->return_values.push_back(return_info);
        
        // 同时更新 function_returns 映射
        data->function_returns[return_info.function_name].push_back(return_info.returned_global);
    }
    return true;
}

bool CacheManager::deserializeFunctionPointerParams(const Json::Value& root) {
    if (!root.isMember("fp_param_info")) return true;
    
    std::lock_guard<std::mutex> lock(data->mutex);
    
    for (const auto& item : root["fp_param_info"]) {
        if (!item.isMember("caller_function") || !item.isMember("fp_name")) continue;
        
        FunctionPointerParamInfo fp_param;
        fp_param.caller_function = item["caller_function"].asString();
        fp_param.fp_name = item["fp_name"].asString();
        fp_param.param_index = item.isMember("param_index") ? item["param_index"].asUInt() : 0;
        fp_param.target_function = item.isMember("target_function") ? item["target_function"].asString() : "";
        fp_param.global_source = item.isMember("global_source") ? item["global_source"].asString() : "";
        fp_param.file = item.isMember("file") ? item["file"].asString() : "";
        fp_param.line = item.isMember("line") ? item["line"].asUInt() : 0;
        data->fp_param_info.push_back(fp_param);
    }
    return true;
}

//=============================================================================
// 辅助方法
//=============================================================================

void CacheManager::clearAllData() {
    std::lock_guard<std::mutex> lock(data->mutex);
    
    data->function_locations.clear();
    data->function_signatures.clear();
    data->call_graph.clear();
    data->global_variables.clear();
    data->pointer_aliases.clear();
    data->function_pointer_calls.clear();
    data->function_pointer_assignments.clear();
    data->function_pointer_targets.clear();
    data->all_writes.clear();
    data->all_calls.clear();
    data->register_ops.clear();
    data->parameter_sources.clear();
    data->param_to_globals.clear();
    data->return_values.clear();
    data->function_returns.clear();
    data->fp_param_info.clear();
    data->call_chains.clear();
}
