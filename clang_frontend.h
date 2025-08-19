#ifndef CLANG_FRONTEND_H
#define CLANG_FRONTEND_H

#include "analysis_data.h"
#include "compilation_database.h"
#include <clang/AST/ASTConsumer.h>
#include <clang/Frontend/ASTConsumers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Tooling/Tooling.h>
#include <memory>

/**
 * Clang AST消费者
 */
class InterruptAnalysisConsumer : public clang::ASTConsumer {
private:
    std::unique_ptr<class InterruptAnalysisVisitor> Visitor;

public:
    explicit InterruptAnalysisConsumer(clang::ASTContext* context, AnalysisData* data, const std::string& file);
    void HandleTranslationUnit(clang::ASTContext& context) override;
};

/**
 * Clang前端动作
 */
class InterruptAnalysisAction : public clang::ASTFrontendAction {
private:
    AnalysisData* Data;
    std::string CurrentFile;

public:
    explicit InterruptAnalysisAction(AnalysisData* data);
    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& CI, clang::StringRef file) override;
};

/**
 * Clang前端动作工厂
 */
class InterruptAnalysisActionFactory : public clang::tooling::FrontendActionFactory {
private:
    AnalysisData* Data;

public:
    explicit InterruptAnalysisActionFactory(AnalysisData* data);
    std::unique_ptr<clang::FrontendAction> create() override;
};

/**
 * Clang前端管理器
 */
class ClangFrontendManager {
private:
    AnalysisData* data;
    std::string compile_db_path;

public:
    explicit ClangFrontendManager(AnalysisData* analysis_data, const std::string& database_path);

    // 运行分析
    bool runAnalysis();

private:
    // 创建Clang工具并运行分析
    bool processBatch(const std::vector<std::string>& files, 
                     std::unique_ptr<clang::tooling::CompilationDatabase>& compilation_db);
    
    // 参数调整器
    clang::tooling::CommandLineArguments adjustArguments(const clang::tooling::CommandLineArguments& args, 
                                                         llvm::StringRef filename);
};

#endif // CLANG_FRONTEND_H
