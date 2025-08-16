#ifndef INTERRUPT_ANALYZER_H
#define INTERRUPT_ANALYZER_H

#include "analysis_data.h"
#include <jsoncpp/json/json.h>
#include <string>
#include <vector>
#include <unordered_set>

// 前向声明
namespace clang {
namespace tooling {
class CompilationDatabase;
}
}

/**
 * 中断处理函数分析器主类
 * 基于 compile_commands.json 进行精确分析
 */
class InterruptAnalyzer {
private:
    AnalysisData data;
    std::string compile_db_path;

public:
    explicit InterruptAnalyzer(const std::string& compile_commands_path);

    /**
     * 分析中断处理函数
     * @param handler_name 处理函数名
     * @param handler_file 处理函数所在文件
     * @return 分析结果JSON
     */
    Json::Value analyzeHandler(const std::string& handler_name, const std::string& handler_file);

    const AnalysisData& getData() const { return data; }

private:
    /**
     * 加载并分析编译数据库
     */
    bool loadAndAnalyzeProject();

    /**
     * 从缓存文件加载分析结果
     */
    bool loadFromCache(const std::string& cache_file);

    /**
     * 保存分析结果到缓存文件
     */
    bool saveToCache(const std::string& cache_file);

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
     * 过滤和统计各种操作
     */
    Json::Value filterWrites(const std::unordered_set<std::string>& reachable);
    Json::Value filterRegisterOperations(const std::unordered_set<std::string>& reachable);
    Json::Value filterFunctionPointerCalls(const std::unordered_set<std::string>& reachable);
    Json::Value generateFunctionPointerAssignments();
    Json::Value generateStatistics(const std::unordered_set<std::string>& reachable,
                                 const std::unordered_set<std::string>& indirect_calls,
                                 const Json::Value& result);

    /**
     * 打印分析统计信息
     */
    void printAnalysisStatistics(long duration_ms);
};

#endif // INTERRUPT_ANALYZER_H
