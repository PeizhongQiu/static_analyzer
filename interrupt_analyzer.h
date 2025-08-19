// interrupt_analyzer.h - 流式处理优化版本
#ifndef INTERRUPT_ANALYZER_H
#define INTERRUPT_ANALYZER_H

#include "analysis_data.h"
#include "cache_manager.h"
#include "clang_frontend.h"
#include "compilation_database.h"
#include "stream_processor.h"
#include <jsoncpp/json/json.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

/**
 * 流式处理配置
 */
struct StreamingConfig {
    size_t max_worker_threads = std::thread::hardware_concurrency();
    size_t max_memory_mb = 500;
    size_t batch_size = 20;
    bool enable_incremental = true;
    bool enable_memory_pressure_relief = true;
    float memory_pressure_threshold = 0.8f;
    bool enable_priority_scheduling = true;
    std::string cache_directory = ".analysis_cache";
};

/**
 * 重构后的中断处理函数分析器主类 - 流式处理版本
 */
class InterruptAnalyzer {
private:
    // 原有成员
    AnalysisData data;
    std::string compile_db_path;
    std::unique_ptr<CacheManager> cache_manager;
    std::unique_ptr<ClangFrontendManager> frontend_manager;
    
    // 新增流式处理成员
    StreamingConfig config;
    std::unique_ptr<StreamProcessor> stream_processor;
    std::unique_ptr<FileResultAggregator> result_aggregator;
    
    // 统计信息
    std::atomic<size_t> total_files{0};
    std::atomic<size_t> processed_files{0};
    std::atomic<size_t> cache_hits{0};
    std::atomic<size_t> error_count{0};

public:
    explicit InterruptAnalyzer(const std::string& compile_commands_path, 
                             const StreamingConfig& cfg = StreamingConfig{});
    ~InterruptAnalyzer();

    /**
     * 流式分析接口
     */
    Json::Value analyzeHandlerStreaming(const std::string& handler_name, const std::string& handler_file);

    /**
     * 配置管理
     */
    void updateConfig(const StreamingConfig& new_config);
    StreamingConfig getConfig() const { return config; }

    /**
     * 获取分析数据 - 保持原有接口
     */
    const AnalysisData& getData() const { return data; }
    
    /**
     * 统计信息
     */
    struct AnalysisStats {
        size_t total_files;
        size_t processed_files;
        size_t cache_hits;
        size_t errors;
        size_t memory_usage_mb;
        double analysis_time_seconds;
        double throughput_files_per_second;
        size_t cache_hit_rate_percent;
    };
    
    AnalysisStats getStatistics() const;

private:
    std::pair<std::unordered_set<std::string>, std::unordered_set<std::string>>
    buildReachableFunctions(const std::string& handler_name);
    Json::Value generateAnalysisResult(const std::string& handler_name,
                                     const std::string& handler_file,
                                     const std::unordered_set<std::string>& reachable,
                                     const std::unordered_set<std::string>& indirect_calls);
    Json::Value filterWrites(const std::unordered_set<std::string>& reachable);
    Json::Value filterRegisterOperations(const std::unordered_set<std::string>& reachable);
    Json::Value filterFunctionPointerCalls(const std::unordered_set<std::string>& reachable);
    Json::Value generateFunctionPointerAssignments();
    Json::Value generateStatistics(const std::unordered_set<std::string>& reachable,
                                 const std::unordered_set<std::string>& indirect_calls,
                                 const Json::Value& result);
    void printAnalysisStatistics(long duration_ms);

    /**
     * 新增流式处理方法
     */
    bool loadAndAnalyzeProjectStreaming();
    void initializeStreamingComponents();
    void cleanupStreamingComponents();
    void printStreamingStatistics();
};

#endif // INTERRUPT_ANALYZER_H
