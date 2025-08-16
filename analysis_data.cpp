#include "analysis_data.h"
#include <algorithm>

void AnalysisData::addFunctionLocation(const std::string& name, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex);
    function_locations[name].push_back(file);
}

void AnalysisData::addFunctionSignature(const std::string& key, const FunctionInfo& info) {
    std::lock_guard<std::mutex> lock(mutex);
    function_signatures[key] = info;
}

void AnalysisData::addCall(const std::string& caller, const std::string& callee) {
    std::lock_guard<std::mutex> lock(mutex);
    call_graph[caller].insert(callee);
}

void AnalysisData::addGlobalVariable(const std::string& var_name, const std::string& file) {
    std::lock_guard<std::mutex> lock(mutex);
    global_variables[var_name] = file;
}

void AnalysisData::addPointerAlias(const std::string& local_var, const std::string& global_path) {
    std::lock_guard<std::mutex> lock(mutex);
    pointer_aliases[local_var] = global_path;
}

void AnalysisData::addFunctionPointerCall(const FunctionPointerCall& fp_call) {
    std::lock_guard<std::mutex> lock(mutex);
    function_pointer_calls.push_back(fp_call);
}

void AnalysisData::addFunctionPointerAssignment(const FunctionPointerAssignment& fp_assign) {
    std::lock_guard<std::mutex> lock(mutex);
    function_pointer_assignments.push_back(fp_assign);
    function_pointer_targets[fp_assign.pointer_name].push_back(fp_assign.target_function);
}

void AnalysisData::addWrite(const WriteOperation& write) {
    std::lock_guard<std::mutex> lock(mutex);
    all_writes.push_back(write);
}

void AnalysisData::addCallInfo(const CallInfo& call) {
    std::lock_guard<std::mutex> lock(mutex);
    all_calls.push_back(call);
}

void AnalysisData::addRegisterOp(const RegisterOperation& reg_op) {
    std::lock_guard<std::mutex> lock(mutex);
    register_ops.push_back(reg_op);
}

bool AnalysisData::isKnownGlobalVariable(const std::string& var_name) {
    std::lock_guard<std::mutex> lock(mutex);
    return global_variables.find(var_name) != global_variables.end();
}

std::string AnalysisData::getGlobalAlias(const std::string& local_var) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = pointer_aliases.find(local_var);
    return (it != pointer_aliases.end()) ? it->second : "";
}

std::vector<std::string> AnalysisData::getFunctionPointerTargets(const std::string& pointer_name) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = function_pointer_targets.find(pointer_name);
    return (it != function_pointer_targets.end()) ? it->second : std::vector<std::string>{};
}
