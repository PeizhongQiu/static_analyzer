#ifndef FUNCTION_POINTER_ANALYSIS_H
#define FUNCTION_POINTER_ANALYSIS_H

#include "analysis_data.h"
#include <clang/AST/AST.h>
#include <string>
#include <vector>

/**
 * 函数指针分析模块
 * 处理函数指针调用、赋值和参数传递
 */
class FunctionPointerAnalyzer {
private:
    clang::ASTContext* Context;
    AnalysisData* Data;
    std::string CurrentFile;
    std::string CurrentFunction;

public:
    explicit FunctionPointerAnalyzer(clang::ASTContext* context, AnalysisData* data, 
                                   const std::string& file, const std::string& function);

    // 函数指针调用分析
    void analyzeFunctionPointerCall(clang::CallExpr* call);
    
    // 函数指针赋值分析
    void analyzeFunctionPointerAssignment(clang::Expr* lhs, clang::Expr* rhs, 
                                         const std::string& type, clang::Stmt* stmt);
    void analyzeFunctionPointerInInitList(clang::InitListExpr* init_list);
    void analyzeFunctionPointerInDesignatedInit(clang::DesignatedInitExpr* designated_init);
    
    // 表达式分析
    std::string extractFunctionPointerName(clang::Expr* expr);
    std::string extractFunctionPointerType(clang::Expr* expr);
    std::string extractFunctionName(clang::Expr* expr);
    std::vector<std::string> findPossibleTargets(const std::string& pointer_name, const std::string& pointer_type);

private:
    void recordFunctionPointerAssignment(const std::string& pointer_name, const std::string& target_func,
                                        const std::string& assignment_type, clang::Stmt* stmt);
};

#endif // FUNCTION_POINTER_ANALYSIS_H
