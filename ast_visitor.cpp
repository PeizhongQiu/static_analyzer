#include "ast_visitor.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>
#include <iostream>
#include <algorithm>
#include <regex>
#include <set>

using namespace clang;

InterruptAnalysisVisitor::InterruptAnalysisVisitor(ASTContext* context, AnalysisData* data, const std::string& file)
    : Context(context), Data(data), CurrentFile(file) {}

//=============================================================================
// AST 节点访问方法
//=============================================================================

bool InterruptAnalysisVisitor::VisitFunctionDecl(FunctionDecl* decl) {
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

bool InterruptAnalysisVisitor::VisitVarDecl(VarDecl* var_decl) {
    // 收集全局变量声明
    if (isGlobalVariableDecl(var_decl)) {
        std::string var_name = var_decl->getNameAsString();
        if (!var_name.empty()) {
            Data->addGlobalVariable(var_name, CurrentFile);
        }
    }
    // 分析局部变量的指针别名
    else if (!CurrentFunction.empty() && var_decl->hasInit()) {
        analyzePointerAlias(var_decl);
    }

    return true;
}

bool InterruptAnalysisVisitor::VisitCallExpr(CallExpr* call) {
    if (CurrentFunction.empty()) return true;

    SourceLocation loc = call->getBeginLoc();
    unsigned line = 0, column = 0;
    if (loc.isValid()) {
        line = Context->getSourceManager().getSpellingLineNumber(loc);
        column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    // 直接函数调用
    if (FunctionDecl* callee = call->getDirectCallee()) {
        recordDirectCall(callee->getNameAsString(), call, line, column);
    }
    // 间接函数调用（函数指针）
    else {
        analyzeFunctionPointerCall(call);
    }

    return true;
}

bool InterruptAnalysisVisitor::VisitBinaryOperator(BinaryOperator* op) {
    if (CurrentFunction.empty()) return true;
    if (!op->isAssignmentOp()) return true;

    Expr* lhs = op->getLHS();
    Expr* rhs = op->getRHS();

    // 检查函数指针赋值
    if (lhs->getType()->isFunctionPointerType()) {
        analyzeFunctionPointerAssignment(lhs, rhs, "direct", op);
    }

    // 检查全局变量写操作
    analyzeWriteOperation(lhs, op, "BinaryOperator");

    return true;
}

bool InterruptAnalysisVisitor::VisitUnaryOperator(UnaryOperator* op) {
    if (CurrentFunction.empty()) return true;

    UnaryOperator::Opcode opcode = op->getOpcode();
    if (opcode == UO_PostInc || opcode == UO_PreInc ||
        opcode == UO_PostDec || opcode == UO_PreDec) {

        analyzeWriteOperation(op->getSubExpr(), op, "UnaryOperator");
    }

    return true;
}

bool InterruptAnalysisVisitor::VisitInitListExpr(InitListExpr* init_list) {
    if (CurrentFunction.empty()) return true;

    analyzeFunctionPointerInInitList(init_list);
    return true;
}

bool InterruptAnalysisVisitor::VisitDesignatedInitExpr(DesignatedInitExpr* designated_init) {
    if (CurrentFunction.empty()) return true;

    analyzeFunctionPointerInDesignatedInit(designated_init);
    return true;
}

bool InterruptAnalysisVisitor::VisitGCCAsmStmt(GCCAsmStmt* asm_stmt) {
    if (CurrentFunction.empty()) return true;

    analyzeInlineAssembly(asm_stmt);
    return true;
}

//=============================================================================
// 核心分析方法
//=============================================================================

FunctionInfo InterruptAnalysisVisitor::extractFunctionInfo(FunctionDecl* decl) {
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

bool InterruptAnalysisVisitor::isGlobalVariableDecl(VarDecl* var_decl) {
    return var_decl->hasGlobalStorage() ||
           var_decl->getStorageClass() == SC_Static ||
           var_decl->hasExternalFormalLinkage();
}

void InterruptAnalysisVisitor::analyzePointerAlias(VarDecl* var_decl) {
    if (!var_decl->getType()->isPointerType()) return;

    std::string var_name = var_decl->getNameAsString();
    Expr* init_expr = var_decl->getInit();

    if (init_expr) {
        std::string global_path = analyzePointerSource(init_expr);
        if (!global_path.empty()) {
            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
            Data->addPointerAlias(full_alias, global_path);
        }
    }
}

void InterruptAnalysisVisitor::recordDirectCall(const std::string& callee_name, CallExpr* call,
                                               unsigned line, unsigned column) {
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

void InterruptAnalysisVisitor::analyzeWriteOperation(Expr* target_expr, Stmt* stmt, const std::string& ast_kind) {
    std::string target = extractWriteTarget(target_expr);

    // 只记录全局变量的修改
    if (!isGlobalVariable(target_expr, target)) {
        return;
    }

    WriteOperation write_op;
    write_op.function = CurrentFunction;
    write_op.file = CurrentFile;
    write_op.ast_kind = ast_kind;
    write_op.target = target;
    write_op.write_type = classifyWriteOperation(target_expr, target);
    write_op.node_id = "write_" + std::to_string(reinterpret_cast<uintptr_t>(stmt));

    SourceLocation loc = stmt->getBeginLoc();
    if (loc.isValid()) {
        write_op.line = Context->getSourceManager().getSpellingLineNumber(loc);
        write_op.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    Data->addWrite(write_op);
}

void InterruptAnalysisVisitor::analyzeFunctionPointerCall(CallExpr* call) {
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

void InterruptAnalysisVisitor::analyzeFunctionPointerAssignment(Expr* lhs, Expr* rhs,
                                                               const std::string& type, Stmt* stmt) {
    std::string pointer_name = extractFunctionPointerName(lhs);
    std::string target_func = extractFunctionName(rhs);

    if (!target_func.empty()) {
        recordFunctionPointerAssignment(pointer_name, target_func, type, stmt);
    }
}

void InterruptAnalysisVisitor::analyzeFunctionPointerInInitList(InitListExpr* init_list) {
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

void InterruptAnalysisVisitor::analyzeFunctionPointerInDesignatedInit(DesignatedInitExpr* designated_init) {
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

void InterruptAnalysisVisitor::recordFunctionPointerAssignment(const std::string& pointer_name,
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

// AST访问器实现 - 第二部分
// 继续第一部分的内容

void InterruptAnalysisVisitor::analyzeInlineAssembly(GCCAsmStmt* asm_stmt) {
    StringLiteral* asm_str = asm_stmt->getAsmString();
    if (!asm_str) return;

    std::string asm_text = asm_str->getString().str();

    // 寄存器操作检测模式
    std::vector<std::pair<std::regex, std::string>> patterns = {
        // 控制寄存器操作
        {std::regex(R"(mov\s+.+,\s*%cr([0-9]+))", std::regex_constants::icase), "cr_write"},
        {std::regex(R"(mov\s+%cr([0-9]+),\s*.+)", std::regex_constants::icase), "cr_read"},

        // 段寄存器操作
        {std::regex(R"(mov\s+.+,\s*%(cs|ds|es|fs|gs|ss))", std::regex_constants::icase), "seg_write"},
        {std::regex(R"(mov\s+%(cs|ds|es|fs|gs|ss),\s*.+)", std::regex_constants::icase), "seg_read"},

        // 通用寄存器操作
        {std::regex(R"(mov\s+.+,\s*%([a-z][a-z0-9]*x?))", std::regex_constants::icase), "reg_write"},
        {std::regex(R"(mov\s+%([a-z][a-z0-9]*x?),\s*.+)", std::regex_constants::icase), "reg_read"},

        // MSR操作
        {std::regex(R"(wrmsr)", std::regex_constants::icase), "msr_write"},
        {std::regex(R"(rdmsr)", std::regex_constants::icase), "msr_read"},

        // 调试寄存器操作
        {std::regex(R"(mov\s+.+,\s*%dr([0-7]))", std::regex_constants::icase), "dr_write"},
        {std::regex(R"(mov\s+%dr([0-7]),\s*.+)", std::regex_constants::icase), "dr_read"},

        // 特权指令
        {std::regex(R"(cli)", std::regex_constants::icase), "interrupt_disable"},
        {std::regex(R"(sti)", std::regex_constants::icase), "interrupt_enable"},
        {std::regex(R"(hlt)", std::regex_constants::icase), "cpu_halt"},
        {std::regex(R"(lgdt)", std::regex_constants::icase), "load_gdt"},
        {std::regex(R"(lidt)", std::regex_constants::icase), "load_idt"},
        {std::regex(R"(lldt)", std::regex_constants::icase), "load_ldt"},
        {std::regex(R"(ltr)", std::regex_constants::icase), "load_tr"}
    };

    for (const auto& [pattern, op_info] : patterns) {
        std::sregex_iterator iter(asm_text.begin(), asm_text.end(), pattern);
        std::sregex_iterator end;

        for (; iter != end; ++iter) {
            RegisterOperation reg_op = parseRegisterOperation(*iter, op_info, asm_stmt, asm_text);
            Data->addRegisterOp(reg_op);
        }
    }
}

RegisterOperation InterruptAnalysisVisitor::parseRegisterOperation(const std::smatch& match,
                                                                  const std::string& op_info,
                                                                  GCCAsmStmt* asm_stmt,
                                                                  const std::string& asm_text) {
    RegisterOperation reg_op;
    reg_op.function = CurrentFunction;
    reg_op.file = CurrentFile;
    reg_op.details = asm_text;

    // 解析操作类型和目标
    if (op_info.find("cr_") == 0 && match.size() > 1) {
        reg_op.target = "cr" + match[1].str();
        reg_op.operation = (op_info == "cr_write") ? "write" : "read";
    } else if (op_info.find("seg_") == 0 && match.size() > 1) {
        reg_op.target = match[1].str();
        reg_op.operation = (op_info == "seg_write") ? "write" : "read";
    } else if (op_info.find("reg_") == 0 && match.size() > 1) {
        reg_op.target = match[1].str();
        reg_op.operation = (op_info == "reg_write") ? "write" : "read";
    } else if (op_info.find("dr_") == 0 && match.size() > 1) {
        reg_op.target = "dr" + match[1].str();
        reg_op.operation = (op_info == "dr_write") ? "write" : "read";
    } else if (op_info == "msr_write") {
        reg_op.target = "msr";
        reg_op.operation = "write";
    } else if (op_info == "msr_read") {
        reg_op.target = "msr";
        reg_op.operation = "read";
    } else {
        // 特权指令
        reg_op.target = op_info;
        reg_op.operation = "execute";
    }

    SourceLocation loc = asm_stmt->getBeginLoc();
    if (loc.isValid()) {
        reg_op.line = Context->getSourceManager().getSpellingLineNumber(loc);
        reg_op.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    reg_op.node_id = "asm_" + std::to_string(reinterpret_cast<uintptr_t>(asm_stmt));

    return reg_op;
}

//=============================================================================
// 表达式分析方法
//=============================================================================

std::string InterruptAnalysisVisitor::analyzePointerSource(Expr* expr) {
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
                return "&" + target_path;
            }
        }
    }

    return "";
}

bool InterruptAnalysisVisitor::isGlobalVariable(Expr* expr, const std::string& target) {
    if (!expr) return false;

    expr = expr->IgnoreImpCasts();

    // 直接的全局变量引用
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        ValueDecl* decl = decl_ref->getDecl();

        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl)) {
            std::string var_name = var_decl->getNameAsString();

            // 检查是否在已知全局变量列表中
            if (Data->isKnownGlobalVariable(var_name)) {
                return true;
            }

            // 检查是否是指向全局变量的局部指针
            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + var_name;
            if (!Data->getGlobalAlias(full_alias).empty()) {
                return true;
            }

            // 通过AST属性判断
            return isGlobalVariableDecl(var_decl);
        }
    }

    // 成员访问
    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        return isGlobalVariable(member->getBase(), "");
    }

    // 数组访问
    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        return isGlobalVariable(array->getBase(), "");
    }

    // 指针解引用
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() == UO_Deref) {
            return isGlobalVariable(unary->getSubExpr(), "");
        }
    }

    // 基于变量名的模式匹配
    if (!target.empty()) {
        std::string base_name = extractBaseName(target);

        // 检查基础变量名
        if (Data->isKnownGlobalVariable(base_name)) {
            return true;
        }

        // 检查是否是已知的指针别名
        std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + base_name;
        if (!Data->getGlobalAlias(full_alias).empty()) {
            return true;
        }
    }

    return false;
}

std::string InterruptAnalysisVisitor::extractWriteTarget(Expr* expr) {
    if (!expr) return "";

    expr = expr->IgnoreImpCasts();

    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        return decl_ref->getDecl()->getNameAsString();
    }

    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        std::string base = extractWriteTarget(member->getBase());
        std::string member_name = member->getMemberDecl()->getNameAsString();
        std::string separator = member->isArrow() ? "->" : ".";
        return base + separator + member_name;
    }

    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() == UO_Deref) {
            std::string sub_target = extractWriteTarget(unary->getSubExpr());
            return "*" + sub_target;
        }
    }

    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        std::string base = extractWriteTarget(array->getBase());
        return base + "[...]";
    }

    return "unknown";
}

//=============================================================================
// 改进的寄存器检测方法
//=============================================================================

std::string InterruptAnalysisVisitor::classifyWriteOperation(Expr* expr, const std::string& target) {
    // 使用改进的寄存器检测策略
    if (isRegisterRelatedVariable(target)) {
        return "register";
    }

    expr = expr->IgnoreImpCasts();

    // 分析指针解引用操作 *ptr = value
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() == UO_Deref) {
            return classifyPointerTarget(unary->getSubExpr());
        }
    }

    // 分析结构体成员访问 obj->member = value 或 obj.member = value
    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        return "data_structure";
    }

    // 分析数组访问 arr[i] = value
    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        return classifyWriteOperation(array->getBase(), extractWriteTarget(array->getBase()));
    }

    // 普通变量赋值
    return "variable";
}

bool InterruptAnalysisVisitor::isRegisterRelatedVariable(const std::string& target) {
    std::string lower_target = target;
    std::transform(lower_target.begin(), lower_target.end(), lower_target.begin(), ::tolower);

    // 策略1: 精确的寄存器名称匹配
    if (isExactRegisterName(lower_target)) {
        return true;
    }

    // 策略2: 寄存器相关的命名模式
    if (isRegisterNamingPattern(lower_target)) {
        return true;
    }

    return false;
}

bool InterruptAnalysisVisitor::isExactRegisterName(const std::string& lower_name) {
    // 精确的寄存器名称列表
    static const std::set<std::string> exact_register_names = {
        // x86/x64 通用寄存器
        "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",

        // 段寄存器
        "cs", "ds", "es", "fs", "gs", "ss",

        // 控制寄存器
        "cr0", "cr1", "cr2", "cr3", "cr4", "cr8",

        // 调试寄存器
        "dr0", "dr1", "dr2", "dr3", "dr6", "dr7",

        // 其他特殊寄存器
        "eflags", "rflags", "eip", "rip"
    };

    return exact_register_names.find(lower_name) != exact_register_names.end();
}

bool InterruptAnalysisVisitor::isRegisterNamingPattern(const std::string& lower_name) {
    // 使用更精确的正则表达式模式
    static const std::vector<std::regex> register_patterns = {
        // 寄存器值/备份变量: reg_value, cr0_val, eax_backup 等
        std::regex(R"(\b(cr[0-8]|dr[0-7]|[re]?[abcd]x|[re]?[sd]i|[re]?[sb]p|[re]?sp)_(val|value|backup|save|restore|old|new|tmp)\b)"),

        // 寄存器状态变量: cr0_state, eflags_status 等
        std::regex(R"(\b(cr[0-8]|dr[0-7]|eflags|rflags)_(state|status|mask|bits)\b)"),

        // 寄存器缓存变量: saved_cr0, old_eax 等
        std::regex(R"(\b(saved|old|new|prev|current)_(cr[0-8]|dr[0-7]|[re]?[abcd]x|eflags)\b)"),

        // MSR相关: msr_*, *_msr
        std::regex(R"(\b(msr_\w+|\w+_msr)\b)"),

        // 寄存器集合: cpu_regs, registers (但要求在特定上下文中)
        std::regex(R"(\bcpu_reg(ister)?s?\b)"),
    };

    for (const auto& pattern : register_patterns) {
        if (std::regex_search(lower_name, pattern)) {
            return true;
        }
    }

    return false;
}

//=============================================================================
// 函数指针和其他表达式分析方法
//=============================================================================

std::string InterruptAnalysisVisitor::extractFunctionPointerName(Expr* expr) {
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

std::string InterruptAnalysisVisitor::extractFunctionPointerType(Expr* expr) {
    if (!expr) return "";

    QualType type = expr->getType();
    if (type->isFunctionPointerType()) {
        return type.getAsString();
    }

    return "";
}

std::string InterruptAnalysisVisitor::extractFunctionName(Expr* expr) {
    if (!expr) return "";

    expr = expr->IgnoreImpCasts();

    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        if (FunctionDecl* func_decl = dyn_cast<FunctionDecl>(decl_ref->getDecl())) {
            return func_decl->getNameAsString();
        }
    }

    return "";
}

std::vector<std::string> InterruptAnalysisVisitor::findPossibleTargets(const std::string& pointer_name,
                                                                       const std::string& pointer_type) {
    // 从已记录的赋值中查找
    return Data->getFunctionPointerTargets(pointer_name);
}

std::string InterruptAnalysisVisitor::extractBaseName(const std::string& target) {
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

//=============================================================================
// 辅助分类方法
//=============================================================================

std::string InterruptAnalysisVisitor::classifyPointerTarget(Expr* ptr_expr) {
    if (!ptr_expr) return "variable";

    ptr_expr = ptr_expr->IgnoreImpCasts();

    // 分析指针表达式的类型
    QualType ptr_type = ptr_expr->getType();

    // 如果是指针类型，分析指向的类型
    if (ptr_type->isPointerType()) {
        QualType pointee_type = ptr_type->getPointeeType();

        // 检查指向的是否是结构体或联合体
        if (pointee_type->isStructureType() || pointee_type->isUnionType()) {
            return "data_structure";
        }

        // 检查是否是函数指针 - 这种情况不应该在写操作中出现
        if (pointee_type->isFunctionType()) {
            return "variable";  // 函数指针解引用应该是调用，不是写操作
        }

        // 指向基本类型的指针，根据上下文分析
        return analyzePointerContext(ptr_expr, pointee_type);
    }

    // 非指针类型，返回变量
    return "variable";
}

std::string InterruptAnalysisVisitor::analyzePointerContext(Expr* ptr_expr, QualType pointee_type) {
    // 检查指针变量本身的来源和上下文
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(ptr_expr)) {
        std::string ptr_name = decl_ref->getDecl()->getNameAsString();

        // 检查是否是指向全局变量的局部指针别名
        std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + ptr_name;
        std::string global_path = Data->getGlobalAlias(full_alias);
        if (!global_path.empty()) {
            return analyzeGlobalPathType(global_path);
        }

        // 检查指针本身是否是全局变量
        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl_ref->getDecl())) {
            if (isGlobalVariableDecl(var_decl)) {
                // 全局指针，根据指向的类型和命名模式判断
                return classifyByNamingConvention(ptr_name, pointee_type);
            }
        }
    }

    // 复杂表达式的指针，根据指向的类型判断
    if (pointee_type->isStructureType() || pointee_type->isUnionType()) {
        return "data_structure";
    }

    return "variable";
}

std::string InterruptAnalysisVisitor::classifyByNamingConvention(const std::string& ptr_name, QualType pointee_type) {
    // 根据指针名称和指向类型的命名惯例进行分类
    std::string lower_name = ptr_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // 检查是否暗示指向数据结构
    std::vector<std::string> struct_indicators = {
        "_dev", "_device", "_info", "_config", "_desc", "_descriptor",
        "_struct", "_data", "_obj", "_object", "_mgr", "_manager",
        "_ctx", "_context", "_state", "_status", "_ctrl", "_control"
    };

    for (const auto& indicator : struct_indicators) {
        if (lower_name.find(indicator) != std::string::npos) {
            return "data_structure";
        }
    }

    // 检查指向的类型名称
    std::string type_name = pointee_type.getAsString();
    std::transform(type_name.begin(), type_name.end(), type_name.begin(), ::tolower);

    if (type_name.find("struct") != std::string::npos ||
        type_name.find("union") != std::string::npos) {
        return "data_structure";
    }

    return "variable";
}

std::string InterruptAnalysisVisitor::analyzeGlobalPathType(const std::string& global_path) {
    // 分析全局变量路径的类型
    // 例如: "global_struct->member" -> data_structure
    //      "global_array[...]" -> variable
    //      "global_device_ptr" -> 根据命名判断

    if (global_path.find("->") != std::string::npos ||
        global_path.find(".") != std::string::npos) {
        return "data_structure";
    }

    if (global_path.find("[") != std::string::npos) {
        return "variable";  // 数组元素访问
    }

    // 检查基础变量名是否暗示是数据结构
    std::string base_name = extractBaseName(global_path);
    return classifyByNamingConvention(base_name, QualType());
}
