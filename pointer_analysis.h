#ifndef POINTER_ANALYSIS_H
#define POINTER_ANALYSIS_H

#include "analysis_data.h"
#include <clang/AST/AST.h>
#include <string>
#include <vector>

/**
 * 指针分析模块
 * 处理指针别名、参数传播、返回值分析等
 */
class PointerAnalyzer {
private:
    clang::ASTContext* Context;
    AnalysisData* Data;
    std::string CurrentFile;
    std::string CurrentFunction;

public:
    explicit PointerAnalyzer(clang::ASTContext* context, AnalysisData* data, 
                           const std::string& file, const std::string& function);

    // 指针别名分析
    void analyzePointerAlias(clang::VarDecl* var_decl);
    std::string analyzePointerSource(clang::Expr* expr);
    
    // 参数传播分析
    void analyzeParameterPropagation(clang::CallExpr* call, clang::FunctionDecl* callee);
    void analyzeMultiLevelCall(clang::CallExpr* call, clang::FunctionDecl* callee);
    std::string analyzeArgumentGlobalSource(clang::Expr* arg);
    std::string analyzeGlobalVariableRef(clang::Expr* expr);
    
    // 返回值分析
    void analyzeReturnValue(clang::ReturnStmt* ret_stmt);
    std::string analyzeReturnExpression(clang::Expr* return_expr);
    
    // 函数指针参数分析
    void analyzeFunctionPointerAsParameter(clang::CallExpr* call, clang::FunctionDecl* callee);
    
    // 指针解析
    std::string resolveComplexPointerAlias(const std::string& target);
    std::string resolveGlobalAlias(const std::string& target);
    std::vector<std::string> getAllPossibleGlobalSources(const std::string& function_name,
                                                         const std::string& param_name);

private:
    // 全局变量检测
    bool isGlobalVariableDecl(clang::VarDecl* var_decl);
    
    // 辅助方法
    std::string extractBaseName(const std::string& target);
    std::string extractArgumentName(clang::Expr* arg);
    std::string getExpressionText(clang::Expr* expr);
};

#endif // POINTER_ANALYSIS_H
