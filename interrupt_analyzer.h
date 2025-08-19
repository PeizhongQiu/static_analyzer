#ifndef INTERRUPT_ANALYZER_H
#define INTERRUPT_ANALYZER_H

#include "analysis_data.h"
#include "cache_manager.h"
#include "clang_frontend.h"
#include "compilation_database.h"
#include <jsoncpp/json/json.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>

/**
 * 重构后的中断处理函数分析器主类
 */
class InterruptAnalyzer {
private:
    AnalysisData data;
    std::string compile_db_path;
    std::unique_ptr<CacheManager> cache_manager;
    std::unique_ptr<ClangFrontendManager> frontend_manager;

public:
    explicit InterruptAnalyzer(const std::string& compile_commands_path);

    /**
     * 分析中断处理函数
     */
    Json::Value analyzeHandler(const std::string& handler_name, const std::string& handler_file);

    /**
     * 获取分析数据
     */
    const AnalysisData& getData() const { return data; }

private:
    /**
     * 加载并分析项目
     */
    bool loadAndAnalyzeProject();

    /**
     * 构建可达函数集合
     */
    std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>>
    buildReachableFunctions(const std::string& handler_name);

    /**
     * 生成分析结果
     */
    Json::Value generateAnalysisResult(const std::string& handler_name,
                                     const std::string& handler_file,
                                     const std::unordered_set<std::string>& reachable,
                                     const std::unordered_set<std::string>& indirect_calls);

    /**
     * 结果生成器
     */
    Json::Value filterWrites(const std::unordered_set<std::string>& reachable);
    Json::Value filterRegisterOperations(const std::unordered_set<std::string>& reachable);
    Json::Value filterFunctionPointerCalls(const std::unordered_set<std::string>& reachable);
    Json::Value generateFunctionPointerAssignments();
    Json::Value generateStatistics(const std::unordered_set<std::string>& reachable,
                                 const std::unordered_set<std::string>& indirect_calls,
                                 const Json::Value& result);

    /**
     * 统计信息输出
     */
    void printAnalysisStatistics(long duration_ms);
};

#endif // INTERRUPT_ANALYZER_H
