#include "pointer_analysis.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Lex/Lexer.h>
#include <algorithm>
#include <iostream>

using namespace clang;

PointerAnalyzer::PointerAnalyzer(ASTContext* context, AnalysisData* data, 
                               const std::string& file, const std::string& function)
    : Context(context), Data(data), CurrentFile(file), CurrentFunction(function) {}

//=============================================================================
// 指针别名分析
//=============================================================================

void PointerAnalyzer::analyzePointerAlias(VarDecl* var_decl) {
    if (!var_decl->getType()->isPointerType()) return;

    std::string var_name = var_decl->getNameAsString();
    Expr* init_expr = var_decl->getInit();

    if (init_expr) {
        init_expr = init_expr->IgnoreImpCasts();
        
        // 检查是否是取地址操作 &global_var
        if (UnaryOperator* unary = dyn_cast<UnaryOperator>(init_expr)) {
            if (unary->getOpcode() == UO_AddrOf) {
                Expr* addr_expr = unary->getSubExpr()->IgnoreImpCasts();
                if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(addr_expr)) {
                    std::string target_name = decl_ref->getDecl()->getNameAsString();
                    
                    // 检查目标是否是全局变量
                    if (VarDecl* target_var = dyn_cast<VarDecl>(decl_ref->getDecl())) {
                        if (isGlobalVariableDecl(target_var)) {
                            // 添加到已知全局变量列表
                            Data->addGlobalVariable(target_name, CurrentFile);
                            // 记录指针别名
                            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
                            Data->addPointerAlias(full_alias, target_name);
                            return;
                        } else if (Data->isKnownGlobalVariable(target_name)) {
                            // 记录指针别名
                            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
                            Data->addPointerAlias(full_alias, target_name);
                            return;
                        }
                    }
                }
            }
        }
        
        // 其他形式的指针初始化
        std::string global_path = analyzePointerSource(init_expr);
        if (!global_path.empty()) {
            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
            Data->addPointerAlias(full_alias, global_path);
        }
    }
}

std::string PointerAnalyzer::analyzePointerSource(Expr* expr) {
    if (!expr) return "";

    expr = expr->IgnoreImpCasts();

    // 直接的全局变量引用
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        ValueDecl* decl = decl_ref->getDecl();
        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl)) {
            std::string var_name = var_decl->getNameAsString();
            if (isGlobalVariableDecl(var_decl) || Data->isKnownGlobalVariable(var_name)) {
                return var_name;
            }
        }
    }

    // 全局变量的成员访问
    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        std::string base_path = analyzePointerSource(member->getBase());
        if (!base_path.empty()) {
            std::string member_name = member->getMemberDecl()->getNameAsString();
            std::string separator = member->isArrow() ? "->" : ".";
            return base_path + separator + member_name;
        }
    }

    // 全局数组的元素访问
    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        std::string base_path = analyzePointerSource(array->getBase());
        if (!base_path.empty()) {
            return base_path + "[...]";
        }
    }

    // 取地址操作
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() == UO_AddrOf) {
            std::string target_path = analyzePointerSource(unary->getSubExpr());
            if (!target_path.empty()) {
                return target_path;
            }
        }
    }

    return "";
}

//=============================================================================
// 参数传播分析
//=============================================================================

void PointerAnalyzer::analyzeParameterPropagation(CallExpr* call, FunctionDecl* callee) {
    if (!callee || !call) return;
    
    // 分析每个参数的来源
    for (unsigned i = 0; i < call->getNumArgs() && i < callee->getNumParams(); ++i) {
        Expr* arg = call->getArg(i);
        ParmVarDecl* param = callee->getParamDecl(i);
        
        if (!arg || !param) continue;
        
        // 分析参数的全局来源
        std::string global_source = analyzeArgumentGlobalSource(arg);
        if (!global_source.empty()) {
            std::string param_key = callee->getNameAsString() + "::" + param->getNameAsString();
            // 这里直接使用 Data 的方法，因为已经在 analyzeMultiLevelCall 中处理了
        }
    }
}

void PointerAnalyzer::analyzeMultiLevelCall(CallExpr* call, FunctionDecl* callee) {
    if (!callee || !call) return;
    
    std::string callee_name = callee->getNameAsString();
    
    // 分析每个参数在调用链中的传播
    for (unsigned i = 0; i < call->getNumArgs() && i < callee->getNumParams(); ++i) {
        Expr* arg = call->getArg(i);
        ParmVarDecl* param = callee->getParamDecl(i);
        
        if (!arg || !param) continue;
        
        std::string param_name = param->getNameAsString();
        
        // 分析参数的所有可能来源
        std::vector<std::string> global_sources;
        
        // 1. 直接的全局变量来源
        std::string direct_global = analyzeArgumentGlobalSource(arg);
        if (!direct_global.empty()) {
            global_sources.push_back(direct_global);
        }
        
        // 2. 来自当前函数参数的传播
        std::string arg_name = extractArgumentName(arg);
        if (!arg_name.empty()) {
            auto current_param_sources = getAllPossibleGlobalSources(CurrentFunction, arg_name);
            global_sources.insert(global_sources.end(), 
                                current_param_sources.begin(), 
                                current_param_sources.end());
        }
        
        // 3. 来自返回值的传播
        if (CallExpr* arg_call = dyn_cast<CallExpr>(arg->IgnoreImpCasts())) {
            if (FunctionDecl* arg_func = arg_call->getDirectCallee()) {
                auto return_sources = Data->resolveFunctionReturns(arg_func->getNameAsString());
                global_sources.insert(global_sources.end(),
                                    return_sources.begin(),
                                    return_sources.end());
            }
        }
        
        // 记录所有发现的参数来源
        for (const auto& global_source : global_sources) {
            ParameterSource param_source;
            param_source.function_name = callee_name;
            param_source.param_name = param_name;
            param_source.param_index = i;
            param_source.global_source = global_source;
            param_source.source_expression = getExpressionText(arg);
            param_source.caller_function = CurrentFunction;
            param_source.file = CurrentFile;
            
            SourceLocation loc = call->getBeginLoc();
            if (loc.isValid()) {
                param_source.line = Context->getSourceManager().getSpellingLineNumber(loc);
            }
            
            Data->addParameterSource(param_source);
        }
    }
}

std::string PointerAnalyzer::analyzeArgumentGlobalSource(Expr* arg) {
    if (!arg) return "";
    
    arg = arg->IgnoreImpCasts();
    
    // 1. 直接的全局变量引用 (&global_var)
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(arg)) {
        if (unary->getOpcode() == UO_AddrOf) {
            Expr* addr_expr = unary->getSubExpr()->IgnoreImpCasts();
            return analyzeGlobalVariableRef(addr_expr);
        }
    }
    
    // 2. 全局变量的成员引用 (&global_struct.member)
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(arg)) {
        if (unary->getOpcode() == UO_AddrOf) {
            Expr* addr_expr = unary->getSubExpr()->IgnoreImpCasts();
            if (MemberExpr* member = dyn_cast<MemberExpr>(addr_expr)) {
                std::string base_global = analyzeGlobalVariableRef(member->getBase());
                if (!base_global.empty()) {
                    std::string member_name = member->getMemberDecl()->getNameAsString();
                    std::string separator = member->isArrow() ? "->" : ".";
                    return base_global + separator + member_name;
                }
            }
        }
    }
    
    // 3. 局部指针变量（需要解析其指向的全局变量）
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(arg)) {
        std::string var_name = decl_ref->getDecl()->getNameAsString();
        std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
        return Data->getGlobalAlias(full_alias);
    }
    
    return "";
}

std::string PointerAnalyzer::analyzeGlobalVariableRef(Expr* expr) {
    if (!expr) return "";
    
    expr = expr->IgnoreImpCasts();
    
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl_ref->getDecl())) {
            std::string var_name = var_decl->getNameAsString();
            if (isGlobalVariableDecl(var_decl) || Data->isKnownGlobalVariable(var_name)) {
                return var_name;
            }
        }
    }
    
    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        std::string base_global = analyzeGlobalVariableRef(member->getBase());
        if (!base_global.empty()) {
            std::string member_name = member->getMemberDecl()->getNameAsString();
            std::string separator = member->isArrow() ? "->" : ".";
            return base_global + separator + member_name;
        }
    }
    
    return "";
}

//=============================================================================
// 返回值分析
//=============================================================================

void PointerAnalyzer::analyzeReturnValue(ReturnStmt* ret_stmt) {
    Expr* return_expr = ret_stmt->getRetValue();
    if (!return_expr) return;
    
    std::string returned_global = analyzeReturnExpression(return_expr);
    if (!returned_global.empty()) {
        ReturnValueInfo return_info;
        return_info.function_name = CurrentFunction;
        return_info.returned_global = returned_global;
        return_info.return_expression = getExpressionText(return_expr);
        return_info.file = CurrentFile;
        
        SourceLocation loc = ret_stmt->getBeginLoc();
        if (loc.isValid()) {
            return_info.line = Context->getSourceManager().getSpellingLineNumber(loc);
        }
        
        Data->addReturnValue(return_info);
    }
}

std::string PointerAnalyzer::analyzeReturnExpression(Expr* return_expr) {
    if (!return_expr) return "";
    
    return_expr = return_expr->IgnoreImpCasts();
    
    // 1. 返回全局变量的地址
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(return_expr)) {
        if (unary->getOpcode() == UO_AddrOf) {
            return analyzeGlobalVariableRef(unary->getSubExpr());
        }
    }
    
    // 2. 返回全局变量
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(return_expr)) {
        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl_ref->getDecl())) {
            std::string var_name = var_decl->getNameAsString();
            if (isGlobalVariableDecl(var_decl) || Data->isKnownGlobalVariable(var_name)) {
                return var_name;
            }
        }
    }
    
    // 3. 返回参数（需要追溯参数来源）
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(return_expr)) {
        if (ParmVarDecl* param_decl = dyn_cast<ParmVarDecl>(decl_ref->getDecl())) {
            std::string param_name = param_decl->getNameAsString();
            auto param_sources = getAllPossibleGlobalSources(CurrentFunction, param_name);
            if (!param_sources.empty()) {
                return param_sources[0]; // 返回第一个可能的来源
            }
        }
    }
    
    // 4. 返回成员访问
    if (MemberExpr* member = dyn_cast<MemberExpr>(return_expr)) {
        std::string base_global = analyzeReturnExpression(member->getBase());
        if (!base_global.empty()) {
            std::string member_name = member->getMemberDecl()->getNameAsString();
            std::string separator = member->isArrow() ? "->" : ".";
            return base_global + separator + member_name;
        }
    }
    
    // 5. 返回函数调用的结果
    if (CallExpr* call_expr = dyn_cast<CallExpr>(return_expr)) {
        if (FunctionDecl* func_decl = call_expr->getDirectCallee()) {
            auto return_sources = Data->resolveFunctionReturns(func_decl->getNameAsString());
            if (!return_sources.empty()) {
                return return_sources[0];
            }
        }
    }
    
    return "";
}

//=============================================================================
// 函数指针参数分析
//=============================================================================

void PointerAnalyzer::analyzeFunctionPointerAsParameter(CallExpr* call, FunctionDecl* callee) {
    if (!callee || !call) return;
    
    for (unsigned i = 0; i < call->getNumArgs() && i < callee->getNumParams(); ++i) {
        Expr* arg = call->getArg(i);
        ParmVarDecl* param = callee->getParamDecl(i);
        
        if (!arg || !param) continue;
        
        // 检查参数类型是否是函数指针
        if (param->getType()->isFunctionPointerType()) {
            FunctionPointerParamInfo fp_param;
            fp_param.caller_function = CurrentFunction;
            fp_param.param_index = i;
            fp_param.file = CurrentFile;
            
            SourceLocation loc = call->getBeginLoc();
            if (loc.isValid()) {
                fp_param.line = Context->getSourceManager().getSpellingLineNumber(loc);
            }
            
            // 分析传递的函数指针
            arg = arg->IgnoreImpCasts();
            
            // 直接的函数引用
            if (DeclRefExpr* func_ref = dyn_cast<DeclRefExpr>(arg)) {
                if (FunctionDecl* target_func = dyn_cast<FunctionDecl>(func_ref->getDecl())) {
                    fp_param.target_function = target_func->getNameAsString();
                    fp_param.fp_name = param->getNameAsString();
                }
            }
            // 全局函数指针变量
            else if (DeclRefExpr* var_ref = dyn_cast<DeclRefExpr>(arg)) {
                if (VarDecl* var_decl = dyn_cast<VarDecl>(var_ref->getDecl())) {
                    std::string var_name = var_decl->getNameAsString();
                    if (isGlobalVariableDecl(var_decl)) {
                        fp_param.fp_name = var_name;
                        fp_param.global_source = var_name;
                        
                        // 查找这个函数指针的可能目标
                        auto targets = Data->getFunctionPointerTargets(var_name);
                        if (!targets.empty()) {
                            fp_param.target_function = targets[0]; // 可以扩展为处理多个目标
                        }
                    }
                }
            }
            
            if (!fp_param.target_function.empty() || !fp_param.global_source.empty()) {
                Data->addFunctionPointerParam(fp_param);
            }
        }
    }
}

//=============================================================================
// 指针解析
//=============================================================================

std::string PointerAnalyzer::resolveComplexPointerAlias(const std::string& target) {
    // 检查是否包含成员访问操作符
    size_t arrow_pos = target.find("->");
    size_t dot_pos = target.find(".");

    if (arrow_pos != std::string::npos || dot_pos != std::string::npos) {
        size_t sep_pos = (arrow_pos != std::string::npos) ? arrow_pos : dot_pos;
        std::string base_var = target.substr(0, sep_pos);
        std::string member_access = target.substr(sep_pos);

        // 获取所有可能的全局来源
        auto global_sources = getAllPossibleGlobalSources(CurrentFunction, base_var);
        if (!global_sources.empty()) {
            // 返回第一个可能的来源（可以扩展为处理多个来源）
            return global_sources[0] + member_access;
        }
        
        // 原有的解析逻辑作为后备
        return resolveGlobalAlias(target);
    }
    
    // 处理没有成员访问的情况
    auto global_sources = getAllPossibleGlobalSources(CurrentFunction, target);
    if (!global_sources.empty()) {
        return global_sources[0];
    }
    
    return resolveGlobalAlias(target);
}

std::string PointerAnalyzer::resolveGlobalAlias(const std::string& target) {
    // 检查是否包含成员访问操作符
    size_t arrow_pos = target.find("->");
    size_t dot_pos = target.find(".");

    if (arrow_pos != std::string::npos || dot_pos != std::string::npos) {
        // 提取基础变量名
        size_t sep_pos = (arrow_pos != std::string::npos) ? arrow_pos : dot_pos;
        std::string base_var = target.substr(0, sep_pos);
        std::string member_access = target.substr(sep_pos);

        // 1. 首先检查是否是函数参数
        auto param_sources = getAllPossibleGlobalSources(CurrentFunction, base_var);
        if (!param_sources.empty()) {
            // 使用第一个可能的来源
            std::string global_source = param_sources[0];
            return global_source + member_access;
        }

        // 2. 检查基础变量是否有局部指针别名
        std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + base_var;
        std::string global_path = Data->getGlobalAlias(full_alias);
        if (!global_path.empty()) {
            return extractBaseName(global_path) + member_access;
        }

        // 3. 检查基础变量是否本身就是全局变量
        if (Data->isKnownGlobalVariable(base_var)) {
            return target;
        }

        return target;
    }

    // 处理没有成员访问的情况
    // 1. 检查是否是函数参数
    auto param_sources = getAllPossibleGlobalSources(CurrentFunction, target);
    if (!param_sources.empty()) {
        return param_sources[0];  // 返回参数的全局来源
    }

    // 2. 原有的逻辑
    if (Data->isKnownGlobalVariable(target)) {
        return target;
    }

    std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + target;
    std::string global_path = Data->getGlobalAlias(full_alias);
    if (!global_path.empty()) {
        return extractBaseName(global_path);
    }

    return target;
}

std::vector<std::string> PointerAnalyzer::getAllPossibleGlobalSources(
    const std::string& function_name, const std::string& param_name) {
    
    return Data->resolveParameterGlobals(function_name, param_name);
}

//=============================================================================
// 私有辅助方法
//=============================================================================

bool PointerAnalyzer::isGlobalVariableDecl(VarDecl* var_decl) {
    return var_decl->hasGlobalStorage() ||
           var_decl->getStorageClass() == SC_Static ||
           var_decl->hasExternalFormalLinkage();
}

std::string PointerAnalyzer::extractBaseName(const std::string& target) {
    std::string base_name = target;

    // 移除前导星号
    if (!base_name.empty() && base_name.front() == '*') {
        base_name = base_name.substr(1);
    }

    // 找到第一个分隔符
    size_t first_sep = std::string::npos;
    std::vector<std::string> separators = {".", "->", "[", " "};
    for (const auto& sep : separators) {
        size_t pos = base_name.find(sep);
        if (pos != std::string::npos) {
            first_sep = std::min(first_sep, pos);
        }
    }

    if (first_sep != std::string::npos) {
        base_name = base_name.substr(0, first_sep);
    }

    return base_name;
}

std::string PointerAnalyzer::extractArgumentName(Expr* arg) {
    if (!arg) return "";
    
    arg = arg->IgnoreImpCasts();
    
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(arg)) {
        return decl_ref->getDecl()->getNameAsString();
    }
    
    return "";
}

std::string PointerAnalyzer::getExpressionText(Expr* expr) {
    if (!expr || !Context) return "";
    
    SourceManager& SM = Context->getSourceManager();
    LangOptions LO;
    
    SourceRange range = expr->getSourceRange();
    if (range.isInvalid()) return "";
    
    return Lexer::getSourceText(CharSourceRange::getTokenRange(range), SM, LO).str();
}
