#ifndef WRITE_ANALYSIS_H
#define WRITE_ANALYSIS_H

#include "analysis_data.h"
#include "pointer_analysis.h"
#include <clang/AST/AST.h>
#include <string>
#include <vector>
#include <set>
#include <regex>

/**
 * 写操作分析模块
 * 处理全局变量写操作检测和分类
 */
class WriteAnalyzer {
private:
    clang::ASTContext* Context;
    AnalysisData* Data;
    PointerAnalyzer* PointerAnalyzer_;
    std::string CurrentFile;
    std::string CurrentFunction;

public:
    explicit WriteAnalyzer(clang::ASTContext* context, AnalysisData* data, 
                          PointerAnalyzer* pointer_analyzer,
                          const std::string& file, const std::string& function);

    // 写操作分析
    void analyzeWriteOperation(clang::Expr* target_expr, clang::Stmt* stmt, const std::string& ast_kind);
    void analyzeFunctionArguments(clang::CallExpr* call, clang::FunctionDecl* callee);
    void analyzePointerArgument(clang::Expr* arg, const std::string& callee_name,
                               unsigned param_index, clang::CallExpr* call,
                               clang::FunctionDecl* callee);

    // 全局变量检测
    bool isGlobalVariableOrIndirect(clang::Expr* expr, const std::string& target);
    std::string extractWriteTarget(clang::Expr* expr);
    
    // 写操作分类
    std::string classifyWriteOperation(clang::Expr* expr, const std::string& target);
    std::string classifyVariableByName(const std::string& var_name, clang::QualType pointee_type);
    std::string classifyPointerTarget(clang::Expr* ptr_expr);

    // 寄存器检测
    bool isRegisterRelatedVariable(const std::string& target);
    bool isExactRegisterName(const std::string& lower_name);
    bool isRegisterNamingPattern(const std::string& lower_name);

private:
    // 字段收集
    std::vector<std::string> collectModifiedFields(clang::FunctionDecl* callee, clang::ParmVarDecl* param);
    
    // 辅助分类方法
    std::string analyzePointerContext(clang::Expr* ptr_expr, clang::QualType pointee_type);
    std::string classifyByNamingConvention(const std::string& ptr_name, clang::QualType pointee_type);
    std::string analyzeGlobalPathType(const std::string& global_path);
    
    // 全局变量检测辅助
    bool isGlobalVariableDecl(clang::VarDecl* var_decl);
};

#endif // WRITE_ANALYSIS_H
