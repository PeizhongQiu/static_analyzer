#include "function_pointer_analysis.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>
#include <iostream>

using namespace clang;

FunctionPointerAnalyzer::FunctionPointerAnalyzer(ASTContext* context, AnalysisData* data, 
                                                const std::string& file, const std::string& function)
    : Context(context), Data(data), CurrentFile(file), CurrentFunction(function) {}

//=============================================================================
// 函数指针调用分析
//=============================================================================

void FunctionPointerAnalyzer::analyzeFunctionPointerCall(CallExpr* call) {
    Expr* callee_expr = call->getCallee();
    if (!callee_expr) return;

    std::string pointer_name = extractFunctionPointerName(callee_expr);
    std::string pointer_type = extractFunctionPointerType(callee_expr);

    FunctionPointerCall fp_call;
    fp_call.caller = CurrentFunction;
    fp_call.pointer_name = pointer_name;
    fp_call.pointer_type = pointer_type;
    fp_call.file = CurrentFile;
    fp_call.node_id = "fp_call_" + std::to_string(reinterpret_cast<uintptr_t>(call));

    SourceLocation loc = call->getBeginLoc();
    if (loc.isValid()) {
        fp_call.line = Context->getSourceManager().getSpellingLineNumber(loc);
        fp_call.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    // 查找可能的目标函数
    fp_call.possible_targets = findPossibleTargets(pointer_name, pointer_type);

    Data->addFunctionPointerCall(fp_call);

    // 将目标函数添加到调用图中
    for (const auto& target : fp_call.possible_targets) {
        Data->addCall(CurrentFunction, target);

        // 记录为间接调用
        CallInfo call_info;
        call_info.caller = CurrentFunction;
        call_info.callee = target;
        call_info.file = CurrentFile;
        call_info.line = fp_call.line;
        call_info.column = fp_call.column;
        call_info.is_indirect = true;
        call_info.node_id = fp_call.node_id + "_to_" + target;

        Data->addCallInfo(call_info);
    }
}

//=============================================================================
// 函数指针赋值分析
//=============================================================================

void FunctionPointerAnalyzer::analyzeFunctionPointerAssignment(Expr* lhs, Expr* rhs,
                                                              const std::string& type, Stmt* stmt) {
    std::string pointer_name = extractFunctionPointerName(lhs);
    std::string target_func = extractFunctionName(rhs);

    if (!target_func.empty()) {
        recordFunctionPointerAssignment(pointer_name, target_func, type, stmt);
    }
}

void FunctionPointerAnalyzer::analyzeFunctionPointerInInitList(InitListExpr* init_list) {
    QualType type = init_list->getType();
    if (!type->isStructureType()) return;

    RecordDecl* record = type->getAsStructureType()->getDecl();
    if (!record) return;

    unsigned field_index = 0;
    for (auto* field : record->fields()) {
        if (field_index >= init_list->getNumInits()) break;

        Expr* init_expr = init_list->getInit(field_index);
        if (init_expr && field->getType()->isFunctionPointerType()) {
            std::string field_name = field->getNameAsString();
            std::string target_func = extractFunctionName(init_expr);

            if (!target_func.empty()) {
                recordFunctionPointerAssignment(field_name, target_func, "struct_init", init_expr);
            }
        }
        field_index++;
    }
}

void FunctionPointerAnalyzer::analyzeFunctionPointerInDesignatedInit(DesignatedInitExpr* designated_init) {
    Expr* init_expr = designated_init->getInit();
    if (!init_expr) return;

    for (const auto& designator : designated_init->designators()) {
        if (designator.isFieldDesignator()) {
            FieldDecl* field = designator.getField();
            if (field && field->getType()->isFunctionPointerType()) {
                std::string field_name = field->getNameAsString();
                std::string target_func = extractFunctionName(init_expr);

                if (!target_func.empty()) {
                    recordFunctionPointerAssignment(field_name, target_func, "designated_init", init_expr);
                }
            }
        }
    }
}

void FunctionPointerAnalyzer::recordFunctionPointerAssignment(const std::string& pointer_name,
                                                             const std::string& target_func,
                                                             const std::string& assignment_type,
                                                             Stmt* stmt) {
    FunctionPointerAssignment fp_assign;
    fp_assign.pointer_name = pointer_name;
    fp_assign.target_function = target_func;
    fp_assign.assignment_type = assignment_type;
    fp_assign.file = CurrentFile;

    SourceLocation loc = stmt->getBeginLoc();
    if (loc.isValid()) {
        fp_assign.line = Context->getSourceManager().getSpellingLineNumber(loc);
        fp_assign.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    Data->addFunctionPointerAssignment(fp_assign);
}

//=============================================================================
// 表达式分析
//=============================================================================

std::string FunctionPointerAnalyzer::extractFunctionPointerName(Expr* expr) {
    if (!expr) return "";

    expr = expr->IgnoreImpCasts();

    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        return decl_ref->getDecl()->getNameAsString();
    }

    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        std::string base = extractFunctionPointerName(member->getBase());
        std::string member_name = member->getMemberDecl()->getNameAsString();
        std::string separator = member->isArrow() ? "->" : ".";
        return base + separator + member_name;
    }

    return "unknown_pointer";
}

std::string FunctionPointerAnalyzer::extractFunctionPointerType(Expr* expr) {
    if (!expr) return "";

    QualType type = expr->getType();
    if (type->isFunctionPointerType()) {
        return type.getAsString();
    }

    return "";
}

std::string FunctionPointerAnalyzer::extractFunctionName(Expr* expr) {
    if (!expr) return "";

    expr = expr->IgnoreImpCasts();

    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        if (FunctionDecl* func_decl = dyn_cast<FunctionDecl>(decl_ref->getDecl())) {
            return func_decl->getNameAsString();
        }
    }

    return "";
}

std::vector<std::string> FunctionPointerAnalyzer::findPossibleTargets(const std::string& pointer_name,
                                                                      const std::string& pointer_type) {
    // 从已记录的赋值中查找
    return Data->getFunctionPointerTargets(pointer_name);
}
