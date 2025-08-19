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

    // 新增：参数来源追踪
    std::vector<ParameterSource> parameter_sources;
    std::unordered_map<std::string, std::vector<std::string>> param_to_globals;
    
    // 新增：返回值追踪
    std::vector<ReturnValueInfo> return_values;
    std::unordered_map<std::string, std::vector<std::string>> function_returns;
    
    // 新增：函数指针参数追踪
    std::vector<FunctionPointerParamInfo> fp_param_info;
    
    // 新增：多层调用链追踪
    std::unordered_map<std::string, std::vector<std::string>> call_chains;

    // 原有数据操作接口
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

    // 新增数据操作接口
    void addParameterSource(const ParameterSource& param_source);
    void addReturnValue(const ReturnValueInfo& return_info);
    void addFunctionPointerParam(const FunctionPointerParamInfo& fp_param);

    // 原有查询接口
    bool isKnownGlobalVariable(const std::string& var_name);
    std::string getGlobalAlias(const std::string& local_var);
    std::vector<std::string> getFunctionPointerTargets(const std::string& pointer_name);

    // 新增查询接口
    std::vector<std::string> resolveParameterGlobals(const std::string& function_name, 
                                                     const std::string& param_name);
    std::vector<std::string> resolveFunctionReturns(const std::string& function_name);
    void buildCallChains();
};

#endif // ANALYSIS_DATA_H
