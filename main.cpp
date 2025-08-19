/**
 * C++ 中断处理函数分析器 - 主程序 (模块化重构版)
 * 使用标准的 CommonOptionsParser
 */

#include "interrupt_analyzer.h"
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <iostream>
#include <fstream>
#include <unordered_map>
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

// 添加帮助信息
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "\n🚀 中断处理函数分析器 v6.0 - 模块化重构版\n"
    "================================================\n"
    "  此工具分析C/C++中断处理函数的完整调用图和副作用。\n"
    "  现已重构为模块化架构，提供更好的可维护性和扩展性。\n\n"
    "新功能:\n"
    "  ✅ 模块化架构设计\n"
    "  ✅ 多层函数调用追踪\n"
    "  ✅ 函数指针参数分析\n"
    "  ✅ 返回值传播分析\n"
    "  ✅ 增强的指针别名解析\n"
    "  ✅ 智能缓存管理\n\n"
    "使用示例:\n"
    "  ./interrupt_analyzer --handler=timer_irq --file=timer.c \\\n"
    "    -p build src/timer.c\n\n"
    "  ./interrupt_analyzer --handler=vm_interrupt \\\n"
    "    --file=drivers/virtio/virtio_mmio.c \\\n"
    "    -p ../kafl.linux ../kafl.linux/drivers/virtio/virtio_mmio.c\n\n"
    "模块结构:\n"
    "  📦 核心模块: analysis_data, ast_visitor_base\n"
    "  🔍 分析模块: pointer_analysis, write_analysis, function_pointer_analysis, assembly_analysis\n"
    "  🏭 基础设施: compilation_database, clang_frontend, cache_manager\n\n"
);

//=============================================================================
// 辅助函数
//=============================================================================

/**
 * 显示结果摘要
 */
void displayResultSummary(const Json::Value& result) {
    std::cout << "\n📊 分析结果摘要:" << std::endl;
    std::cout << "   可达函数: " << result["statistics"]["reachable_functions"].asInt() << std::endl;

    if (result["statistics"]["indirect_call_functions"].asInt() > 0) {
        std::cout << "   函数指针调用: " << result["statistics"]["indirect_call_functions"].asInt() << std::endl;
    }

    std::cout << "   全局变量写操作: " << result["statistics"]["total_writes"].asInt() << std::endl;
    std::cout << "   寄存器操作: " << result["statistics"]["register_operations"].asInt() << std::endl;

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

    // 函数指针调用预览
    if (result["function_pointer_calls"].size() > 0) {
        std::cout << "\n🔗 函数指针调用: " << result["function_pointer_calls"].size() << " 个" << std::endl;
    }

    // 寄存器操作预览
    if (result["register_operations"].size() > 0) {
        std::cout << "🔧 寄存器操作: " << result["register_operations"].size() << " 个" << std::endl;
    }

    // 显示一些回溯成功的写操作示例
    std::cout << "\n🎯 成功回溯的写操作示例:" << std::endl;
    int shown_count = 0;
    for (const auto& write : result["filtered_writes"]) {
        std::string target = write["target"].asString();
        if (target.find("->") != std::string::npos || target.find(".") != std::string::npos) {
            std::cout << "   " << write["function"].asString() << ": " << target 
                      << " (" << write["write_type"].asString() << ")" << std::endl;
            shown_count++;
            if (shown_count >= 5) break;  // 只显示前5个
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
std::string getOutputFileWithHandler(const std::string& base, const std::string& handler) {
    std::string filename = base;
    auto pos = filename.rfind('.');
    if (pos != std::string::npos) {
        filename.insert(pos, "_" + handler);
    } else {
        filename += "_" + handler;
    }
    return filename;
}

/**
 * 显示模块化架构信息
 */
void displayArchitectureInfo() {
    std::cout << "\n🏗️ 模块化架构信息:" << std::endl;
    std::cout << "   📦 核心模块:" << std::endl;
    std::cout << "     - analysis_data: 线程安全的数据存储" << std::endl;
    std::cout << "     - ast_visitor_base: AST访问器基础框架" << std::endl;
    std::cout << "   🔍 分析模块:" << std::endl;
    std::cout << "     - pointer_analysis: 指针别名和参数追踪" << std::endl;
    std::cout << "     - write_analysis: 全局变量写操作检测" << std::endl;
    std::cout << "     - function_pointer_analysis: 函数指针处理" << std::endl;
    std::cout << "     - assembly_analysis: 内联汇编分析" << std::endl;
    std::cout << "   🏭 基础设施:" << std::endl;
    std::cout << "     - compilation_database: 编译数据库处理" << std::endl;
    std::cout << "     - clang_frontend: Clang工具链管理" << std::endl;
    std::cout << "     - cache_manager: 智能结果缓存" << std::endl;
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

    // 显示欢迎信息
    std::string final_output = getOutputFileWithHandler(OutputFile, HandlerName);

    std::cout << "===============================================" << std::endl;
    std::cout << "🚀 C++ 中断处理函数分析器 v6.0 - 模块化重构版" << std::endl;
    std::cout << "===============================================" << std::endl;
    std::cout << "🎯 目标函数: " << HandlerName << std::endl;
    std::cout << "📄 目标文件: " << HandlerFile << std::endl;
    std::cout << "📋 编译数据库: " << compile_commands_path << std::endl;
    std::cout << "💾 输出文件: " << final_output << std::endl;
    std::cout << "📁 源文件数量: " << options_parser.getSourcePathList().size() << std::endl;
    std::cout << "===============================================" << std::endl;
    
    displayArchitectureInfo();

    // 创建分析器并运行
    InterruptAnalyzer analyzer(compile_commands_path);
    Json::Value result = analyzer.analyzeHandler(HandlerName, HandlerFile);

    // 检查是否有错误
    if (result.isMember("error")) {
        std::cout << "❌ 分析错误: " << result["error"].asString() << std::endl;
        return 1;
    }

    // 保存结果
    if (saveResults(result, final_output)) {
        std::cout << "\n💾 结果已保存到: " << final_output << std::endl;
    } else {
        std::cout << "⚠️ 无法保存结果到文件: " << final_output << std::endl;
    }

    // 显示结果摘要
    displayResultSummary(result);

    std::cout << "\n🎉 模块化分析完成！现在应该能正确回溯 head->next 等复杂写操作了。" << std::endl;
    std::cout << "💡 提示: 使用 'make show-modules' 查看完整的模块结构" << std::endl;

    return 0;
}
