// stream_processor.h - 修复版本，添加缺失的方法声明
#ifndef STREAM_PROCESSOR_H
#define STREAM_PROCESSOR_H

#include "analysis_data.h"
#include "compilation_database.h"
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <unordered_map>

/**
 * 流式分析任务
 */
struct StreamingTask {
    std::string file_path;
    std::string content_hash;
    size_t file_size;
    int priority;
    
    bool operator<(const StreamingTask& other) const {
        return priority > other.priority; // 优先队列，小值优先
    }
};

/**
 * 文件分析结果
 */
struct FileAnalysisResult {
    std::string file_path;
    bool success = false;
    std::string error_message;
    
    // 分析数据
    std::vector<WriteOperation> writes;
    std::vector<CallInfo> calls;
    std::vector<FunctionPointerCall> fp_calls;
    std::vector<RegisterOperation> register_ops;
    std::vector<ParameterSource> parameter_sources;
    std::vector<ReturnValueInfo> return_values;
    std::unordered_map<std::string, FunctionInfo> functions;
    std::unordered_map<std::string, std::string> global_vars;
    std::unordered_map<std::string, std::string> pointer_aliases;
};

/**
 * 流式处理结果回调接口
 */
class IStreamingCallback {
public:
    virtual ~IStreamingCallback() = default;
    virtual void onFileResult(const FileAnalysisResult& result) = 0;
    virtual void onProgress(float progress, const std::string& current_file) = 0;
    virtual void onError(const std::string& file_path, const std::string& error) = 0;
    virtual void onComplete() = 0;
};

/**
 * 流式处理器
 */
class StreamProcessor {
private:
    // 线程池
    std::vector<std::thread> worker_threads;
    std::atomic<bool> should_stop{false};
    
    // 任务队列
    std::priority_queue<StreamingTask> task_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    
    // 回调
    std::unique_ptr<IStreamingCallback> callback;
    
    // 增量分析缓存
    std::unordered_map<std::string, std::string> file_hashes;
    std::unordered_map<std::string, FileAnalysisResult> cached_results;
    std::mutex cache_mutex;
    
    // 统计信息
    std::atomic<size_t> processed_files{0};
    std::atomic<size_t> total_files{0};
    std::atomic<size_t> cache_hits{0};
    std::atomic<size_t> error_count{0};
    
    // 配置
    size_t max_worker_threads;
    size_t max_memory_usage;
    std::atomic<size_t> current_memory_usage{0};
    
    // 编译数据库路径
    std::string compile_db_path;

public:
    explicit StreamProcessor(size_t num_threads = std::thread::hardware_concurrency());
    ~StreamProcessor();
    
    // 配置
    void setCallback(std::unique_ptr<IStreamingCallback> cb);
    void setMaxMemoryUsage(size_t max_bytes);
    void setCompileDbPath(const std::string& path);
    
    // 流式处理控制
    void start();
    void stop();
    void addTask(const StreamingTask& task);
    void addTasks(const std::vector<StreamingTask>& tasks);
    
    // 状态查询
    float getProgress() const;
    size_t getCacheHitRate() const;
    size_t getCurrentMemoryUsage() const;
    size_t getProcessedFiles() const { return processed_files; }
    size_t getErrorCount() const { return error_count; }
    
private:
    void workerLoop();
    bool processTask(const StreamingTask& task);
    bool needsReanalysis(const StreamingTask& task);
    std::string calculateFileHash(const std::string& file_path);
    void manageMemory();
    void evictOldResults();
    size_t estimateResultSize(const FileAnalysisResult& result);
};

/**
 * 单文件分析器 - 修复版本
 */
class SingleFileAnalyzer {
private:
    std::string file_path;
    std::string compile_db_path;
    std::unique_ptr<AnalysisData> local_data;
    
    // 共享的编译数据库处理器，避免重复加载
    static std::unique_ptr<CompilationDatabaseProcessor> shared_db_processor;
    static std::mutex db_processor_mutex;

public:
    explicit SingleFileAnalyzer(const std::string& file, const std::string& compile_db);
    bool analyze();
    void extractResults(FileAnalysisResult& result);

private:
    bool setupCompilationArgs(std::vector<std::string>& args);
    bool runClangAnalysis(const std::vector<std::string>& args);
    static CompilationDatabaseProcessor* getSharedDbProcessor(const std::string& compile_db_path);
    
    // 新增的编译参数处理方法
    std::vector<std::string> cleanCompilationArgs(const std::vector<std::string>& original_args, 
                                                  const std::string& working_dir);
    bool shouldSkipArg(const std::string& arg);
    bool isUsefulArg(const std::string& arg);
    std::vector<std::string> getMinimalCompilationArgs();
};

/**
 * 文件结果聚合器
 */
class FileResultAggregator : public IStreamingCallback {
private:
    AnalysisData* target_data;
    std::mutex aggregation_mutex;
    
    // 批处理
    std::vector<FileAnalysisResult> pending_batch;
    size_t batch_size_threshold;
    
    // 内存管理
    size_t max_memory_threshold;
    std::atomic<size_t> current_batch_size{0};
    
    // 统计
    std::atomic<size_t> processed_count{0};
    std::atomic<size_t> error_count{0};

public:
    explicit FileResultAggregator(AnalysisData* data, 
                                size_t batch_size = 20,
                                size_t memory_limit = 100 * 1024 * 1024);
    
    // IStreamingCallback 接口实现
    void onFileResult(const FileAnalysisResult& result) override;
    void onProgress(float progress, const std::string& current_file) override;
    void onError(const std::string& file_path, const std::string& error) override;
    void onComplete() override;
    
    // 统计信息
    size_t getProcessedCount() const { return processed_count; }
    size_t getErrorCount() const { return error_count; }
    
private:
    void processBatch();
    void flushPendingResults();
    size_t estimateResultSize(const FileAnalysisResult& result);
    void aggregateResult(const FileAnalysisResult& result);
};

/**
 * 任务调度器 - 改进版本
 */
class TaskScheduler {
public:
    static std::vector<StreamingTask> createTasks(const std::vector<std::string>& files);
    static int calculateFilePriority(const std::string& file_path, size_t file_size);
    static std::string calculateFileHash(const std::string& file_path);
    
private:
    static bool isHeaderFile(const std::string& file_path);
    static bool isCriticalPath(const std::string& file_path);
    static bool shouldSkipFile(const std::string& file_path);
    static bool isCppSourceFile(const std::string& file_path);
};

#endif // STREAM_PROCESSOR_H
