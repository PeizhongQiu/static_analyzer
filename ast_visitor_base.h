#ifndef AST_VISITOR_BASE_H
#define AST_VISITOR_BASE_H

#include "analysis_data.h"
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/AST.h>
#include <clang/Lex/Lexer.h>
#include <string>
#include <vector>
#include <unordered_map>

// Forward declaration
class InterruptAnalysisVisitor;

/**
 * AST访问器基础类
 * 包含基本的访问方法和成员变量
 */
class InterruptAnalysisVisitorBase : public clang::RecursiveASTVisitor<InterruptAnalysisVisitor> {
protected:
    clang::ASTContext* Context;
    AnalysisData* Data;
    std::string CurrentFile;
    std::string CurrentFunction;

    // 多层调用链分析
    std::vector<std::string> current_call_stack;
    std::unordered_map<std::string, int> recursion_depth;
    
    // 参数和返回值追踪
    std::unordered_map<std::string, std::vector<ParameterSource>> function_param_map;
    std::unordered_map<std::string, std::vector<ReturnValueInfo>> function_return_map;

public:
    explicit InterruptAnalysisVisitorBase(clang::ASTContext* context, AnalysisData* data, const std::string& file);
    
    // 基础AST节点访问方法 - 声明为虚函数以便子类重写
    virtual bool VisitFunctionDecl(clang::FunctionDecl* decl);
    virtual bool VisitVarDecl(clang::VarDecl* var_decl);
    virtual bool VisitCallExpr(clang::CallExpr* call);
    virtual bool VisitBinaryOperator(clang::BinaryOperator* op);
    virtual bool VisitUnaryOperator(clang::UnaryOperator* op);
    virtual bool VisitInitListExpr(clang::InitListExpr* init_list);
    virtual bool VisitDesignatedInitExpr(clang::DesignatedInitExpr* designated_init);
    virtual bool VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt);
    virtual bool VisitReturnStmt(clang::ReturnStmt* ret_stmt);

protected:
    // 核心分析方法 - 由子类实现
    virtual FunctionInfo extractFunctionInfo(clang::FunctionDecl* decl);
    virtual bool isGlobalVariableDecl(clang::VarDecl* var_decl);
    virtual void recordDirectCall(const std::string& callee_name, clang::CallExpr* call, unsigned line, unsigned column);

    // 调用栈管理
    void enterFunction(const std::string& function_name);
    void exitFunction(const std::string& function_name);
    bool isRecursiveCall(const std::string& function_name);

    // 辅助方法
    std::string extractArgumentName(clang::Expr* arg);
    std::string getExpressionText(clang::Expr* expr);
};

#endif // AST_VISITOR_BASE_H
