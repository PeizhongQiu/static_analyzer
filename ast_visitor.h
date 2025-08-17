#ifndef AST_VISITOR_H
#define AST_VISITOR_H
#include "analysis_data.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/AST.h>
#include <string>
#include <vector>
#include <regex>

/**
 * 中断分析AST访问器
 */
class InterruptAnalysisVisitor : public clang::RecursiveASTVisitor<InterruptAnalysisVisitor> {
private:
    clang::ASTContext* Context;
    AnalysisData* Data;
    std::string CurrentFile;
    std::string CurrentFunction;

public:
    explicit InterruptAnalysisVisitor(clang::ASTContext* context, AnalysisData* data, const std::string& file);
    
    // AST 节点访问方法
    bool VisitFunctionDecl(clang::FunctionDecl* decl);
    bool VisitVarDecl(clang::VarDecl* var_decl);
    bool VisitCallExpr(clang::CallExpr* call);
    bool VisitBinaryOperator(clang::BinaryOperator* op);
    bool VisitUnaryOperator(clang::UnaryOperator* op);
    bool VisitInitListExpr(clang::InitListExpr* init_list);
    bool VisitDesignatedInitExpr(clang::DesignatedInitExpr* designated_init);
    bool VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt);

private:
    // 核心分析方法
    FunctionInfo extractFunctionInfo(clang::FunctionDecl* decl);
    bool isGlobalVariableDecl(clang::VarDecl* var_decl);
    void analyzePointerAlias(clang::VarDecl* var_decl);
    void recordDirectCall(const std::string& callee_name, clang::CallExpr* call, unsigned line, unsigned column);
    void analyzeWriteOperation(clang::Expr* target_expr, clang::Stmt* stmt, const std::string& ast_kind);
    void analyzeFunctionPointerCall(clang::CallExpr* call);
    void analyzeFunctionPointerAssignment(clang::Expr* lhs, clang::Expr* rhs, const std::string& type, clang::Stmt* stmt);
    void analyzeFunctionPointerInInitList(clang::InitListExpr* init_list);
    void analyzeFunctionPointerInDesignatedInit(clang::DesignatedInitExpr* designated_init);
    void recordFunctionPointerAssignment(const std::string& pointer_name, const std::string& target_func,
                                       const std::string& assignment_type, clang::Stmt* stmt);
    void analyzeInlineAssembly(clang::GCCAsmStmt* asm_stmt);
    RegisterOperation parseRegisterOperation(const std::smatch& match, const std::string& op_info,
                                           clang::GCCAsmStmt* asm_stmt, const std::string& asm_text);
    
    // 新增：函数参数分析方法
    void analyzeFunctionArguments(clang::CallExpr* call, clang::FunctionDecl* callee);
    void analyzePointerArgument(clang::Expr* arg, const std::string& callee_name, 
                               unsigned param_index, clang::CallExpr* call);
    
    // 表达式分析方法
    std::string analyzePointerSource(clang::Expr* expr);
    bool isGlobalVariable(clang::Expr* expr, const std::string& target);
    
    // 新增：增强的全局变量检测
    bool isGlobalVariableOrIndirect(clang::Expr* expr, const std::string& target);
    
    std::string extractWriteTarget(clang::Expr* expr);
    std::string classifyWriteOperation(clang::Expr* expr, const std::string& target);
    std::string extractFunctionPointerName(clang::Expr* expr);
    std::string extractFunctionPointerType(clang::Expr* expr);
    std::string extractFunctionName(clang::Expr* expr);
    std::vector<std::string> findPossibleTargets(const std::string& pointer_name, const std::string& pointer_type);
    std::string extractBaseName(const std::string& target);
    
    // 新增：从目标字符串中提取基础变量名
    std::string extractBaseNameFromTarget(const std::string& target);
    
    bool matchesKernelGlobalPattern(const std::string& name);
   
    // Resolve local aliases to their underlying global variable
    std::string resolveGlobalAlias(const std::string& target);

    // 改进的寄存器检测方法
    bool isRegisterRelatedVariable(const std::string& target);
    bool isExactRegisterName(const std::string& lower_name);
    bool isRegisterNamingPattern(const std::string& lower_name);
    
    // 辅助分类方法
    std::string classifyPointerTarget(clang::Expr* ptr_expr);
    std::string analyzePointerContext(clang::Expr* ptr_expr, clang::QualType pointee_type);
    std::string classifyByNamingConvention(const std::string& ptr_name, clang::QualType pointee_type);
    std::string analyzeGlobalPathType(const std::string& global_path);
    
    // 新增：根据变量名和类型进行分类
    std::string classifyVariableByName(const std::string& var_name, clang::QualType pointee_type);
};

#endif // AST_VISITOR_H
