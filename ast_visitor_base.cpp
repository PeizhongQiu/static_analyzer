#include "ast_visitor_base.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>
#include <iostream>

using namespace clang;

InterruptAnalysisVisitorBase::InterruptAnalysisVisitorBase(ASTContext* context, AnalysisData* data, const std::string& file)
    : Context(context), Data(data), CurrentFile(file) {}

//=============================================================================
// 基础AST节点访问方法
//=============================================================================

bool InterruptAnalysisVisitorBase::VisitFunctionDecl(FunctionDecl* decl) {
    if (!decl->hasBody()) {
        return true; // 只处理有函数体的函数定义
    }

    std::string func_name = decl->getNameAsString();
    CurrentFunction = func_name;

    // 提取函数详细信息
    FunctionInfo info = extractFunctionInfo(decl);

    // 保存函数信息
    Data->addFunctionLocation(func_name, CurrentFile);
    std::string sig_key = CurrentFile + ":" + func_name;
    Data->addFunctionSignature(sig_key, info);

    return true;
}

bool InterruptAnalysisVisitorBase::VisitVarDecl(VarDecl* var_decl) {
    std::string var_name = var_decl->getNameAsString();
    
    // 收集全局变量声明
    if (isGlobalVariableDecl(var_decl)) {
        if (!var_name.empty()) {
            Data->addGlobalVariable(var_name, CurrentFile);
        }
    }

    return true;
}

bool InterruptAnalysisVisitorBase::VisitCallExpr(CallExpr* call) {
    if (CurrentFunction.empty()) return true;

    SourceLocation loc = call->getBeginLoc();
    unsigned line = 0, column = 0;
    if (loc.isValid()) {
        line = Context->getSourceManager().getSpellingLineNumber(loc);
        column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    if (FunctionDecl* callee = call->getDirectCallee()) {
        std::string callee_name = callee->getNameAsString();
        
        // 防止无限递归
        if (!isRecursiveCall(callee_name)) {
            enterFunction(callee_name);
            recordDirectCall(callee_name, call, line, column);
            exitFunction(callee_name);
        }
    }

    return true;
}

bool InterruptAnalysisVisitorBase::VisitBinaryOperator(BinaryOperator* op) {
    // 基础实现，子类可以重写
    return true;
}

bool InterruptAnalysisVisitorBase::VisitUnaryOperator(UnaryOperator* op) {
    // 基础实现，子类可以重写
    return true;
}

bool InterruptAnalysisVisitorBase::VisitInitListExpr(InitListExpr* init_list) {
    // 基础实现，子类可以重写
    return true;
}

bool InterruptAnalysisVisitorBase::VisitDesignatedInitExpr(DesignatedInitExpr* designated_init) {
    // 基础实现，子类可以重写
    return true;
}

bool InterruptAnalysisVisitorBase::VisitGCCAsmStmt(GCCAsmStmt* asm_stmt) {
    // 基础实现，子类可以重写
    return true;
}

bool InterruptAnalysisVisitorBase::VisitReturnStmt(ReturnStmt* ret_stmt) {
    // 基础实现，子类可以重写
    return true;
}

//=============================================================================
// 核心分析方法 - 基础实现
//=============================================================================

FunctionInfo InterruptAnalysisVisitorBase::extractFunctionInfo(FunctionDecl* decl) {
    FunctionInfo info;
    info.name = decl->getNameAsString();
    info.file = CurrentFile;

    SourceLocation loc = decl->getLocation();
    if (loc.isValid()) {
        info.line = Context->getSourceManager().getSpellingLineNumber(loc);
    }

    // 返回类型
    info.return_type = decl->getReturnType().getAsString();

    // 参数列表
    for (unsigned i = 0; i < decl->getNumParams(); ++i) {
        ParmVarDecl* param = decl->getParamDecl(i);
        std::string param_str = param->getType().getAsString();
        if (!param->getNameAsString().empty()) {
            param_str += " " + param->getNameAsString();
        }
        info.parameters.push_back(param_str);
    }

    // 静态属性和链接属性
    info.is_static = (decl->getStorageClass() == SC_Static);
    info.linkage = "none";
    if (decl->hasLinkage()) {
        switch (decl->getLinkageAndVisibility().getLinkage()) {
            case ExternalLinkage: info.linkage = "external"; break;
            case InternalLinkage: info.linkage = "internal"; break;
            default: info.linkage = "other"; break;
        }
    }

    return info;
}

bool InterruptAnalysisVisitorBase::isGlobalVariableDecl(VarDecl* var_decl) {
    return var_decl->hasGlobalStorage() ||
           var_decl->getStorageClass() == SC_Static ||
           var_decl->hasExternalFormalLinkage();
}

void InterruptAnalysisVisitorBase::recordDirectCall(const std::string& callee_name, CallExpr* call, unsigned line, unsigned column) {
    // 添加到调用图
    Data->addCall(CurrentFunction, callee_name);

    // 记录详细调用信息
    CallInfo call_info;
    call_info.caller = CurrentFunction;
    call_info.callee = callee_name;
    call_info.file = CurrentFile;
    call_info.line = line;
    call_info.column = column;
    call_info.is_indirect = false;
    call_info.node_id = "call_" + std::to_string(reinterpret_cast<uintptr_t>(call));

    Data->addCallInfo(call_info);
}

//=============================================================================
// 调用栈管理
//=============================================================================

void InterruptAnalysisVisitorBase::enterFunction(const std::string& function_name) {
    current_call_stack.push_back(function_name);
    recursion_depth[function_name]++;
}

void InterruptAnalysisVisitorBase::exitFunction(const std::string& function_name) {
    if (!current_call_stack.empty() && current_call_stack.back() == function_name) {
        current_call_stack.pop_back();
    }
    recursion_depth[function_name]--;
}

bool InterruptAnalysisVisitorBase::isRecursiveCall(const std::string& function_name) {
    return recursion_depth[function_name] > 2; // 限制递归深度
}

//=============================================================================
// 辅助方法
//=============================================================================

std::string InterruptAnalysisVisitorBase::extractArgumentName(Expr* arg) {
    if (!arg) return "";
    
    arg = arg->IgnoreImpCasts();
    
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(arg)) {
        return decl_ref->getDecl()->getNameAsString();
    }
    
    return "";
}

std::string InterruptAnalysisVisitorBase::getExpressionText(Expr* expr) {
    if (!expr || !Context) return "";
    
    SourceManager& SM = Context->getSourceManager();
    LangOptions LO;
    
    SourceRange range = expr->getSourceRange();
    if (range.isInvalid()) return "";
    
    return Lexer::getSourceText(CharSourceRange::getTokenRange(range), SM, LO).str();
}
