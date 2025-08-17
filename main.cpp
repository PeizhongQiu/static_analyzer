/**
 * C++ 中断处理函数分析器 - 主程序
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
    "\n中断处理函数分析器使用说明:\n"
    "  此工具分析C/C++中断处理函数的完整调用图和副作用。\n"
    "  使用 -p 指定包含 compile_commands.json 的目录。\n\n"
    "示例:\n"
    "  ./interrupt_analyzer --handler=timer_irq --file=timer.c \\\n"
    "    -p build src/timer.c\n\n"
    "  ./interrupt_analyzer --handler=vm_interrupt \\\n"
    "    --file=drivers/virtio/virtio_mmio.c \\\n"
    "    -p ../kafl.linux ../kafl.linux/drivers/virtio/virtio_mmio.c\n\n"
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

    // 直接使用 InterruptAnalyzer 类，传入编译数据库路径
    // 从 -p 参数获取编译数据库目录
    std::string compile_commands_path;
    
    // 获取编译数据库，并从中推导路径
    auto& compilation_db = options_parser.getCompilations();
    
    // 通过编译数据库获取一个编译命令，从中提取目录
    auto source_paths = options_parser.getSourcePathList();
    if (!source_paths.empty()) {
        auto compile_commands = compilation_db.getCompileCommands(source_paths[0]);
        if (!compile_commands.empty()) {
            std::string build_dir = compile_commands[0].Directory;
            compile_commands_path = build_dir + "/compile_commands.json";
        }
    }
    
    // 如果上面的方法失败，尝试默认路径
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
        compile_commands_path = "compile_commands.json"; // 最后的默认值
    }
    
    std::cout << "📋 使用编译数据库: " << compile_commands_path << std::endl;

    // 显示欢迎信息
    std::string final_output = getOutputFileWithHandler(OutputFile, HandlerName);

    std::cout << "============================================" << std::endl;
    std::cout << "🚀 C++ 中断处理函数分析器 v4.0" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "🎯 目标函数: " << HandlerName << std::endl;
    std::cout << "📄 目标文件: " << HandlerFile << std::endl;
    std::cout << "📋 编译数据库: " << compile_commands_path << std::endl;
    std::cout << "💾 输出文件: " << final_output << std::endl;
    std::cout << "📁 源文件数量: " << options_parser.getSourcePathList().size() << std::endl;
    std::cout << "============================================" << std::endl;

    InterruptAnalyzer analyzer(compile_commands_path);
    
    // 使用原来的分析方法
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

    std::cout << "\n🎉 分析完成！" << std::endl;

    return 0;
}
