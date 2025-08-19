#include "clang_frontend.h"
#include "ast_visitor.h"
#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/FileSystem.h>
#include <iostream>
#include <unordered_set>

using namespace clang;
using namespace clang::tooling;

//=============================================================================
// InterruptAnalysisConsumer
//=============================================================================

InterruptAnalysisConsumer::InterruptAnalysisConsumer(ASTContext* context, AnalysisData* data, const std::string& file) {
    Visitor = std::make_unique<InterruptAnalysisVisitor>(context, data, file);
}

void InterruptAnalysisConsumer::HandleTranslationUnit(ASTContext& context) {
    Visitor->TraverseDecl(context.getTranslationUnitDecl());
}

//=============================================================================
// InterruptAnalysisAction
//=============================================================================

InterruptAnalysisAction::InterruptAnalysisAction(AnalysisData* data) : Data(data) {}

std::unique_ptr<ASTConsumer> InterruptAnalysisAction::CreateASTConsumer(CompilerInstance& CI, StringRef file) {
    CurrentFile = file.str();
    return std::make_unique<InterruptAnalysisConsumer>(&CI.getASTContext(), Data, CurrentFile);
}

//=============================================================================
// InterruptAnalysisActionFactory
//=============================================================================

InterruptAnalysisActionFactory::InterruptAnalysisActionFactory(AnalysisData* data) : Data(data) {}

std::unique_ptr<FrontendAction> InterruptAnalysisActionFactory::create() {
    return std::make_unique<InterruptAnalysisAction>(Data);
}

//=============================================================================
// ClangFrontendManager
//=============================================================================

ClangFrontendManager::ClangFrontendManager(AnalysisData* analysis_data, const std::string& database_path)
    : data(analysis_data), compile_db_path(database_path) {}

bool ClangFrontendManager::runAnalysis() {
    // 加载编译数据库处理器
    CompilationDatabaseProcessor db_processor(compile_db_path);
    if (!db_processor.loadCompileCommands()) {
        return false;
    }

    // 直接创建编译数据库和工具
    std::string error_msg;
    auto compilation_db = CompilationDatabase::autoDetectFromDirectory(
        llvm::sys::path::parent_path(compile_db_path), error_msg);
    
    if (!compilation_db) {
        // 跳过重复的 --no-warn 选项
        if (arg == "--no-warn" || arg == "-w") {
            if (seen_options.find("no-warn") == seen_options.end()) {
                seen_options.insert("no-warn");
                adjusted_args.push_back("-w");  // 使用简短形式
            }
            continue;
        }
        
        // 跳过其他可能导致问题的选项
        if (arg.find("--help") == 0 || arg.find("--version") == 0 ||
            arg.find("-march=") == 0 || arg.find("-mcpu=") == 0) {
            continue;
        }
        
        adjusted_args.push_back(arg);
    }
    
    return adjusted_args;
} 如果自动检测失败，尝试加载固定的编译数据库
        compilation_db = CompilationDatabase::loadFromDirectory(
            llvm::sys::path::parent_path(compile_db_path), error_msg);
    }
    
    if (!compilation_db) {
        std::cout << "❌ 无法加载编译数据库: " << error_msg << std::endl;
        return false;
    }

    // 分批处理源文件
    auto file_batches = db_processor.createFileBatches(50);
    std::cout << "📊 分成 " << file_batches.size() << " 批进行处理" << std::endl;

    // 串行处理每批文件
    for (size_t i = 0; i < file_batches.size(); ++i) {
        std::cout << "🔄 处理第 " << (i + 1) << "/" << file_batches.size() << " 批文件..." << std::endl;
        
        if (!processBatch(file_batches[i], compilation_db)) {
            std::cout << "⚠️ 第 " << (i + 1) << " 批文件分析时出现错误，继续处理..." << std::endl;
        }
    }

    return true;
}

bool ClangFrontendManager::processBatch(const std::vector<std::string>& files, 
                                       std::unique_ptr<CompilationDatabase>& compilation_db) {
    // 创建 Clang 工具
    ClangTool tool(*compilation_db, files);
    
    // 添加参数调整器来清理选项
    tool.appendArgumentsAdjuster([this](const CommandLineArguments& args, llvm::StringRef filename) {
        return adjustArguments(args, filename);
    });

    // 运行分析
    InterruptAnalysisActionFactory factory(data);
    int analysis_result = tool.run(&factory);

    return analysis_result == 0;
}

CommandLineArguments ClangFrontendManager::adjustArguments(const CommandLineArguments& args, 
                                                          llvm::StringRef filename) {
    CommandLineArguments adjusted_args;
    std::unordered_set<std::string> seen_options;
    
    for (const auto& arg : args) {
        // 跳过重复的 --no-warn 选项
        if (arg == "--no-warn" || arg == "-w") {
            if (seen_options.find("no-warn") == seen_options.end()) {
                seen_options.insert("no-warn");
                adjusted_args.push_back("-w");  // 使用简短形式
            }
            continue;
        }
                
        // 跳过其他可能导致问题的选项
        if (arg.find("--help") == 0 || arg.find("--version") == 0 ||
            arg.find("-march=") == 0 || arg.find("-mcpu=") == 0) {
            continue;
        }
                
        adjusted_args.push_back(arg);
    }
            
    return adjusted_args;
} 
