/**
 * C++ 中断处理函数分析器 - 主程序 (流式处理优化版)
 * 支持传统模式和流式处理模式
 */

#include "interrupt_analyzer.h"
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <unordered_map>
#include <chrono>
#include <jsoncpp/json/json.h>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;

//=============================================================================
// 命令行选项
//=============================================================================

static cl::OptionCategory AnalyzerCategory("Interrupt Analyzer Options");

static cl::opt<std::string> HandlerName("handler",
    cl::desc("Interrupt handler function name"),
    cl::Required,
    cl::cat(AnalyzerCategory));

static cl::opt<std::string> HandlerFile("file",
    cl::desc("Handler file path"),
    cl::Required,
    cl::cat(AnalyzerCategory));

static cl::opt<std::string> OutputFile("output",
    cl::desc("Output JSON file"),
    cl::init("analysis_result.json"),
    cl::cat(AnalyzerCategory));

// 流式处理选项
static cl::opt<bool> EnableStreaming("streaming",
    cl::desc("Enable streaming processing mode"),
    cl::init(true),
    cl::cat(AnalyzerCategory));

static cl::opt<unsigned> MaxWorkerThreads("worker-threads",
    cl::desc("Maximum number of worker threads"),
    cl::init(0), // 0 表示使用硬件并发数
    cl::cat(AnalyzerCategory));

static cl::opt<unsigned> MaxMemoryMB("max-memory",
    cl::desc("Maximum memory usage in MB"),
    cl::init(500*1024),
    cl::cat(AnalyzerCategory));

static cl::opt<unsigned> BatchSize("batch-size",
    cl::desc("Batch size for streaming processing"),
    cl::init(20),
    cl::cat(AnalyzerCategory));

static cl::opt<bool> EnableIncremental("incremental",
    cl::desc("Enable incremental analysis"),
    cl::init(true),
    cl::cat(AnalyzerCategory));

static cl::opt<bool> EnableMemoryPressureRelief("memory-relief",
    cl::desc("Enable memory pressure relief"),
    cl::init(true),
    cl::cat(AnalyzerCategory));

static cl::opt<std::string> CacheDirectory("cache-dir",
    cl::desc("Cache directory for incremental analysis"),
    cl::init(".analysis_cache"),
    cl::cat(AnalyzerCategory));

// 添加帮助信息
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "\n🚀 中断处理函数分析器 v7.0 - 流式处理优化版\n"
    "================================================\n"
    "  此工具分析C/C++中断处理函数的完整调用图和副作用。\n"
    "  现已优化为流式处理架构，支持大型项目的高效分析。\n\n"
    "流式处理特性:\n"
    "  ✅ 多线程并行分析\n"
    "  ✅ 内存使用优化\n"
    "  ✅ 增量分析支持\n"
    "  ✅ 智能任务调度\n"
    "  ✅ 实时进度报告\n"
    "  ✅ 内存压力管理\n\n"
    "使用示例:\n"
    "  # 流式处理模式（推荐）\n"
    "  ./interrupt_analyzer --handler=timer_irq --file=timer.c \\\n"
    "    --streaming --worker-threads=8 --max-memory=1024 \\\n"
    "    -p build src/timer.c\n\n"
    "  # 传统模式（兼容性）\n"
    "  ./interrupt_analyzer --handler=vm_interrupt \\\n"
    "    --file=drivers/virtio/virtio_mmio.c \\\n"
    "    --no-streaming \\\n"
    "    -p ../kafl.linux ../kafl.linux/drivers/virtio/virtio_mmio.c\n\n"
    "性能调优选项:\n"
    "  --worker-threads=N 工作线程数（默认: CPU核心数）\n"
    "  --max-memory=N     最大内存使用量MB（默认: 500）\n"
    "  --batch-size=N     批处理大小（默认: 20）\n"
    "  --incremental      启用增量分析（默认: 开启）\n"
    "  --memory-relief    启用内存压力管理（默认: 开启）\n\n"
);

//=============================================================================
// 辅助函数
//=============================================================================

/**
 * 创建流式处理配置
 */
StreamingConfig createStreamingConfig() {
    StreamingConfig config;
    
    if (MaxWorkerThreads.getValue() > 0) {
        config.max_worker_threads = MaxWorkerThreads.getValue();
    }
    config.max_memory_mb = MaxMemoryMB.getValue();
    config.batch_size = BatchSize.getValue();
    config.enable_incremental = EnableIncremental.getValue();
    config.enable_memory_pressure_relief = EnableMemoryPressureRelief.getValue();
    config.cache_directory = CacheDirectory.getValue();
    
    return config;
}

/**
 * 显示配置信息
 */
void displayConfiguration(const StreamingConfig& config, bool streaming_mode) {
    std::cout << "⚙️  分析配置:" << std::endl;
    std::cout << "   处理模式: " << (streaming_mode ? "流式处理" : "传统批处理") << std::endl;
    
    if (streaming_mode) {
        std::cout << "   工作线程: " << config.max_worker_threads << std::endl;
        std::cout << "   内存限制: " << config.max_memory_mb << " MB" << std::endl;
        std::cout << "   批处理大小: " << config.batch_size << std::endl;
        std::cout << "   增量分析: " << (config.enable_incremental ? "启用" : "禁用") << std::endl;
        std::cout << "   内存管理: " << (config.enable_memory_pressure_relief ? "启用" : "禁用") << std::endl;
        std::cout << "   缓存目录: " << config.cache_directory << std::endl;
    }
}

/**
 * 显示结果摘要
 */
void displayResultSummary(const Json::Value& result, bool streaming_mode) {
    std::cout << "\n📊 分析结果摘要:" << std::endl;
    std::cout << "   可达函数: " << result["statistics"]["reachable_functions"].asInt() << std::endl;

    if (result["statistics"]["indirect_call_functions"].asInt() > 0) {
        std::cout << "   函数指针调用: " << result["statistics"]["indirect_call_functions"].asInt() << std::endl;
    }

    std::cout << "   全局变量写操作: " << result["statistics"]["total_writes"].asInt() << std::endl;
    std::cout << "   寄存器操作: " << result["statistics"]["register_operations"].asInt() << std::endl;

    // 流式处理特有统计
    if (streaming_mode && result.isMember("streaming_stats")) {
        std::cout << "\n🚀 流式处理统计:" << std::endl;
        std::cout << "   总文件数: " << result["streaming_stats"]["total_files"].asInt() << std::endl;
        std::cout << "   缓存命中率: " << result["streaming_stats"]["cache_hit_rate"].asInt() << "%" << std::endl;
        
        if (result["streaming_stats"]["throughput"].asDouble() > 0) {
            std::cout << "   处理吞吐: " << std::fixed << std::setprecision(1) 
                      << result["streaming_stats"]["throughput"].asDouble() << " 文件/秒" << std::endl;
        }
        
        std::cout << "   内存使用: " << result["streaming_stats"]["memory_usage_mb"].asInt() << " MB" << std::endl;
    }

    // 写操作分类统计
    if (result["filtered_writes"].size() > 0) {
        std::unordered_map<std::string, int> write_type_counts;
        for (const auto& write : result["filtered_writes"]) {
            std::string write_type = write["write_type"].asString();
            write_type_counts[write_type]++;
        }

        std::cout << "\n📝 写操作分类:" << std::endl;
        for (const auto& [type, count] : write_type_counts) {
            std::string type_desc;
            if (type == "variable") type_desc = "变量";
            else if (type == "data_structure") type_desc = "数据结构";
            else if (type == "register") type_desc = "寄存器";
            else type_desc = type;

            std::cout << "   " << type_desc << ": " << count << std::endl;
        }
    }

    // 显示一些成功回溯的写操作示例
    std::cout << "\n🎯 成功回溯的写操作示例:" << std::endl;
    int shown_count = 0;
    for (const auto& write : result["filtered_writes"]) {
        std::string target = write["target"].asString();
        if (target.find("->") != std::string::npos || target.find(".") != std::string::npos) {
            std::cout << "   " << write["function"].asString() << ": " << target 
                      << " (" << write["write_type"].asString() << ")" << std::endl;
            shown_count++;
            if (shown_count >= 5) break;
        }
    }
    
    if (shown_count == 0) {
        std::cout << "   (暂无复杂的成员访问写操作)" << std::endl;
    }
}

/**
 * 保存分析结果到文件
 */
bool saveResults(const Json::Value& result, const std::string& filename) {
    std::ofstream output(filename);
    if (!output.is_open()) {
        return false;
    }

    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(result, &output);

    return true;
}

/**
 * 根据 handler 名生成输出文件名
 */
std::string getOutputFileWithHandler(const std::string& base, const std::string& handler, bool streaming) {
    std::string filename = base;
    auto pos = filename.rfind('.');
    std::string suffix = "_" + handler;
    if (streaming) {
        suffix += "_streaming";
    }
    
    if (pos != std::string::npos) {
        filename.insert(pos, suffix);
    } else {
        filename += suffix;
    }
    return filename;
}

/**
 * 显示性能建议
 */
void displayPerformanceAdvice(const Json::Value& result, const StreamingConfig& config) {
    if (!result.isMember("streaming_stats")) return;
    
    auto stats = result["streaming_stats"];
    std::cout << "\n💡 性能优化建议:" << std::endl;
    
    // 缓存命中率建议
    int cache_hit_rate = stats["cache_hit_rate"].asInt();
    if (cache_hit_rate < 30) {
        std::cout << "   ⚠️ 缓存命中率较低 (" << cache_hit_rate << "%)，考虑启用增量分析" << std::endl;
    } else if (cache_hit_rate > 80) {
        std::cout << "   ✅ 缓存命中率很高 (" << cache_hit_rate << "%)，增量分析工作良好" << std::endl;
    }
    
    // 内存使用建议
    int memory_usage = stats["memory_usage_mb"].asInt();
    if (memory_usage > config.max_memory_mb * 0.9) {
        std::cout << "   ⚠️ 内存使用接近上限，建议增加 --max-memory 参数或减少 --batch-size" << std::endl;
    } else if (memory_usage < config.max_memory_mb * 0.3) {
        std::cout << "   💡 内存使用较低，可适当增加 --batch-size 提升性能" << std::endl;
    }
    
    // 线程建议
    if (config.max_worker_threads < std::thread::hardware_concurrency()) {
        std::cout << "   💡 可考虑增加线程数到 " << std::thread::hardware_concurrency() 
                  << " 充分利用CPU资源 (使用 --worker-threads=" << std::thread::hardware_concurrency() << ")" << std::endl;
    }
}

//=============================================================================
// 主函数
//=============================================================================

int main(int argc, const char** argv) {
    auto expected_parser = CommonOptionsParser::create(argc, argv, AnalyzerCategory);
    
    if (!expected_parser) {
        errs() << "命令行解析错误: " << toString(expected_parser.takeError()) << "\n";
        return 1;
    }

    CommonOptionsParser& options_parser = expected_parser.get();

    // 推导编译数据库路径
    std::string compile_commands_path;
    
    auto& compilation_db = options_parser.getCompilations();
    auto source_paths = options_parser.getSourcePathList();
    if (!source_paths.empty()) {
        auto compile_commands = compilation_db.getCompileCommands(source_paths[0]);
        if (!compile_commands.empty()) {
            std::string build_dir = compile_commands[0].Directory;
            compile_commands_path = build_dir + "/compile_commands.json";
        }
    }
    
    // 回退路径
    if (compile_commands_path.empty() || !llvm::sys::fs::exists(compile_commands_path)) {
        std::vector<std::string> fallback_paths = {
            "compile_commands.json",
            "../compile_commands.json",
            "../kafl.linux/compile_commands.json"
        };
        
        for (const auto& path : fallback_paths) {
            if (llvm::sys::fs::exists(path)) {
                compile_commands_path = path;
                break;
            }
        }
    }
    
    if (compile_commands_path.empty()) {
        compile_commands_path = "compile_commands.json";
    }

    // 创建配置
    StreamingConfig config = createStreamingConfig();
    bool streaming_mode = EnableStreaming.getValue();
    
    // 生成输出文件名
    std::string final_output = getOutputFileWithHandler(OutputFile, HandlerName, streaming_mode);

    // 显示欢迎信息
    std::cout << "===============================================" << std::endl;
    std::cout << "🚀 C++ 中断处理函数分析器 v7.0 - 流式处理优化版" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "🎯 目标函数: " << HandlerName << std::endl;
    std::cout << "📄 目标文件: " << HandlerFile << std::endl;
    std::cout << "📋 编译数据库: " << compile_commands_path << std::endl;
    std::cout << "💾 输出文件: " << final_output << std::endl;
    std::cout << "📁 源文件数量: " << options_parser.getSourcePathList().size() << std::endl;
    std::cout << "===============================================" << std::endl;
    
    displayConfiguration(config, streaming_mode);

    // 创建分析器
    InterruptAnalyzer analyzer(compile_commands_path, config);
    
    Json::Value result;
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 根据模式选择分析方法
    if (streaming_mode) {
        std::cout << "\n🚀 启动流式处理模式..." << std::endl;
        result = analyzer.analyzeHandlerStreaming(HandlerName, HandlerFile);
    } else {
        std::cout << "\n🔄 启动传统分析模式..." << std::endl;
        result = analyzer.analyzeHandler(HandlerName, HandlerFile);
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    // 检查是否有错误
    if (result.isMember("error")) {
        std::cout << "❌ 分析错误: " << result["error"].asString() << std::endl;
        return 1;
    }

    // 添加性能指标到结果中
    result["performance_metrics"] = Json::Value();
    result["performance_metrics"]["total_time_ms"] = static_cast<int>(duration.count());
    result["performance_metrics"]["processing_mode"] = streaming_mode ? "streaming" : "traditional";
    
    // 保存结果
    if (saveResults(result, final_output)) {
        std::cout << "\n💾 结果已保存到: " << final_output << std::endl;
    } else {
        std::cout << "⚠️ 无法保存结果到文件: " << final_output << std::endl;
    }

    // 显示结果摘要
    displayResultSummary(result, streaming_mode);
    
    // 显示性能建议
    if (streaming_mode) {
        displayPerformanceAdvice(result, config);
    }

    std::cout << "\n🎉 " << (streaming_mode ? "流式" : "传统") << "分析完成！" 
              << "总耗时: " << duration.count() << " ms" << std::endl;
    
    if (streaming_mode) {
        std::cout << "💡 提示: 流式处理模式已优化内存使用和处理速度" << std::endl;
        std::cout << "💡 提示: 使用 --no-streaming 可切换到传统模式进行对比" << std::endl;
    } else {
        std::cout << "💡 提示: 使用 --streaming 启用流式处理模式获得更好性能" << std::endl;
    }

    return 0;
}
