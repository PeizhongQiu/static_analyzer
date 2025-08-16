#ifndef ANALYSIS_DATA_H
#define ANALYSIS_DATA_H
#include "data_structures.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>

/**
 * 线程安全的分析数据存储
 */
class AnalysisData {
public:
    std::mutex mutex;

    // 函数相关数据
    std::unordered_map<std::string, std::vector<std::string>> function_locations;
    std::unordered_map<std::string, FunctionInfo> function_signatures;
    std::unordered_map<std::string, std::unordered_set<std::string>> call_graph;

    // 全局变量相关
    std::unordered_map<std::string, std::string> global_variables;
    std::unordered_map<std::string, std::string> pointer_aliases;

    // 函数指针相关
    std::vector<FunctionPointerCall> function_pointer_calls;
    std::vector<FunctionPointerAssignment> function_pointer_assignments;
    std::unordered_map<std::string, std::vector<std::string>> function_pointer_targets;

    // 操作记录
    std::vector<WriteOperation> all_writes;
    std::vector<CallInfo> all_calls;
    std::vector<RegisterOperation> register_ops;

    // 数据操作接口
    void addFunctionLocation(const std::string& name, const std::string& file);
    void addFunctionSignature(const std::string& key, const FunctionInfo& info);
    void addCall(const std::string& caller, const std::string& callee);
    void addGlobalVariable(const std::string& var_name, const std::string& file);
    void addPointerAlias(const std::string& local_var, const std::string& global_path);
    void addFunctionPointerCall(const FunctionPointerCall& fp_call);
    void addFunctionPointerAssignment(const FunctionPointerAssignment& fp_assign);
    void addWrite(const WriteOperation& write);
    void addCallInfo(const CallInfo& call);
    void addRegisterOp(const RegisterOperation& reg_op);

    // 查询接口
    bool isKnownGlobalVariable(const std::string& var_name);
    std::string getGlobalAlias(const std::string& local_var);
    std::vector<std::string> getFunctionPointerTargets(const std::string& pointer_name);
};

#endif // ANALYSIS_DATA_H
