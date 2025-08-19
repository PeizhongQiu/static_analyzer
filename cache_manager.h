#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include "analysis_data.h"
#include <jsoncpp/json/json.h>
#include <string>

/**
 * 分析结果缓存管理器
 */
class CacheManager {
private:
    AnalysisData* data;
    std::string cache_file_path;

public:
    explicit CacheManager(AnalysisData* analysis_data, const std::string& cache_file = "analysis_cache.json");

    // 缓存操作
    bool loadFromCache();
    bool saveToCache();
    bool cacheExists() const;
    void clearCache();

private:
    // JSON序列化
    Json::Value serializeFunctionLocations();
    Json::Value serializeFunctionSignatures();
    Json::Value serializeGlobalVariables();
    Json::Value serializePointerAliases();
    Json::Value serializeCallGraph();
    Json::Value serializeFunctionPointerTargets();
    Json::Value serializeWrites();
    Json::Value serializeCalls();
    Json::Value serializeRegisterOps();
    Json::Value serializeFunctionPointerCalls();
    Json::Value serializeFunctionPointerAssignments();
    Json::Value serializeParameterSources();
    Json::Value serializeReturnValues();
    Json::Value serializeFunctionPointerParams();

    // JSON反序列化
    bool deserializeFunctionLocations(const Json::Value& root);
    bool deserializeFunctionSignatures(const Json::Value& root);
    bool deserializeGlobalVariables(const Json::Value& root);
    bool deserializePointerAliases(const Json::Value& root);
    bool deserializeCallGraph(const Json::Value& root);
    bool deserializeFunctionPointerTargets(const Json::Value& root);
    bool deserializeWrites(const Json::Value& root);
    bool deserializeCalls(const Json::Value& root);
    bool deserializeRegisterOps(const Json::Value& root);
    bool deserializeFunctionPointerCalls(const Json::Value& root);
    bool deserializeFunctionPointerAssignments(const Json::Value& root);
    bool deserializeParameterSources(const Json::Value& root);
    bool deserializeReturnValues(const Json::Value& root);
    bool deserializeFunctionPointerParams(const Json::Value& root);

    // 辅助方法
    void clearAllData();
};

#endif // CACHE_MANAGER_H
