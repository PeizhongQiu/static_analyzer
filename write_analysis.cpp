#include "write_analysis.h"
#include <clang/AST/AST.h>
#include <clang/AST/RecursiveASTVisitor.h>  // 添加这个头文件
#include <clang/Basic/SourceManager.h>
#include <algorithm>
#include <set>
#include <regex>
#include <iostream>

using namespace clang;

WriteAnalyzer::WriteAnalyzer(ASTContext* context, AnalysisData* data, 
                           PointerAnalyzer* pointer_analyzer,
                           const std::string& file, const std::string& function)
    : Context(context), Data(data), PointerAnalyzer_(pointer_analyzer), 
      CurrentFile(file), CurrentFunction(function) {}

//=============================================================================
// 写操作分析
//=============================================================================

void WriteAnalyzer::analyzeWriteOperation(Expr* target_expr, Stmt* stmt, const std::string& ast_kind) {
    std::string target = extractWriteTarget(target_expr);

    // 检查是否是全局变量或通过指针的间接访问
    bool is_global = isGlobalVariableOrIndirect(target_expr, target);
    
    if (!is_global) {
        return;
    }

    std::string resolved_target = PointerAnalyzer_->resolveComplexPointerAlias(target);
    
    // 生成唯一的节点ID，避免重复记录
    std::string node_id = "write_" + std::to_string(reinterpret_cast<uintptr_t>(stmt)) + "_" + ast_kind;
    
    WriteOperation write_op;
    write_op.function = CurrentFunction;
    write_op.file = CurrentFile;
    write_op.ast_kind = ast_kind;
    write_op.target = resolved_target;
    write_op.write_type = classifyWriteOperation(target_expr, resolved_target);
    write_op.node_id = node_id;

    SourceLocation loc = stmt->getBeginLoc();
    if (loc.isValid()) {
        write_op.line = Context->getSourceManager().getSpellingLineNumber(loc);
        write_op.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    Data->addWrite(write_op);
}

void WriteAnalyzer::analyzeFunctionArguments(CallExpr* call, FunctionDecl* callee) {
    if (!callee) return;

    // 获取函数参数类型信息
    for (unsigned i = 0; i < call->getNumArgs() && i < callee->getNumParams(); ++i) {
        Expr* arg = call->getArg(i);
        ParmVarDecl* param = callee->getParamDecl(i);
        
        if (!arg || !param) continue;

        QualType param_type = param->getType();
        
        // 检查是否是指针参数（可能会被修改）
        if (param_type->isPointerType()) {
            analyzePointerArgument(arg, callee->getNameAsString(), i, call, callee);
        }
    }
}

void WriteAnalyzer::analyzePointerArgument(Expr* arg, const std::string& callee_name,
                                         unsigned param_index, CallExpr* call,
                                         FunctionDecl* callee) {
    if (!arg) return;
    
    arg = arg->IgnoreImpCasts();
    
    // 只处理直接传递全局变量的情况，不处理局部指针
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(arg)) {
        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl_ref->getDecl())) {
            std::string var_name = var_decl->getNameAsString();
            
            // 只记录全局变量
            if (isGlobalVariableDecl(var_decl) || Data->isKnownGlobalVariable(var_name)) {
                std::vector<std::string> fields;
                if (callee && param_index < callee->getNumParams()) {
                    fields = collectModifiedFields(callee, callee->getParamDecl(param_index));
                }

                if (fields.empty()) {
                    fields.push_back(var_name);
                }

                for (const auto& field : fields) {
                    WriteOperation write_op;
                    write_op.function = CurrentFunction;
                    write_op.file = CurrentFile;
                    write_op.ast_kind = "FunctionCall";

                    std::string target = field;
                    if (callee && param_index < callee->getNumParams()) {
                        std::string param_name = callee->getParamDecl(param_index)->getNameAsString();
                        if (target.rfind(param_name, 0) == 0) {
                            target = var_name + target.substr(param_name.length());
                        }
                    }

                    write_op.target = target;
                    write_op.write_type = classifyWriteOperation(arg, var_name);
                    write_op.node_id = "func_arg_" +
                        std::to_string(reinterpret_cast<uintptr_t>(call)) + "_param_" +
                        std::to_string(param_index);

                    SourceLocation loc = call->getBeginLoc();
                    if (loc.isValid()) {
                        write_op.line = Context->getSourceManager().getSpellingLineNumber(loc);
                        write_op.column = Context->getSourceManager().getSpellingColumnNumber(loc);
                    }

                    Data->addWrite(write_op);
                }
            }
        }
    }
}

//=============================================================================
// 全局变量检测
//=============================================================================

bool WriteAnalyzer::isGlobalVariableOrIndirect(Expr* expr, const std::string& target) {
    if (!expr) return false;

    expr = expr->IgnoreImpCasts();

    // 1. 直接的全局变量引用
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        ValueDecl* decl = decl_ref->getDecl();

        if (VarDecl* var_decl = dyn_cast<VarDecl>(decl)) {
            std::string var_name = var_decl->getNameAsString();

            // 检查是否在已知全局变量列表中
            if (Data->isKnownGlobalVariable(var_name)) {
                return true;
            }

            // 通过AST属性判断
            if (isGlobalVariableDecl(var_decl)) {
                // 将新发现的全局变量添加到数据中
                Data->addGlobalVariable(var_name, CurrentFile);
                return true;
            }
        }
    }

    // 2. 成员访问 (obj->member 或 obj.member)
    if (MemberExpr* member = dyn_cast<MemberExpr>(expr)) {
        Expr* base = member->getBase()->IgnoreImpCasts();

        // 检查基础表达式是否指向全局变量
        if (DeclRefExpr* base_decl = dyn_cast<DeclRefExpr>(base)) {
            std::string base_name = base_decl->getDecl()->getNameAsString();

            // 检查是否是指向全局变量的指针别名
            std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + base_name;
            std::string global_path = Data->getGlobalAlias(full_alias);
            if (!global_path.empty()) {
                return true;
            }

            // 检查基础变量本身是否是全局变量
            if (VarDecl* base_var = dyn_cast<VarDecl>(base_decl->getDecl())) {
                if (isGlobalVariableDecl(base_var) || Data->isKnownGlobalVariable(base_name)) {
                    return true;
                }
            }
        }

        // 递归检查基础表达式
        return isGlobalVariableOrIndirect(base, "");
    }

    // 3. 指针解引用 (*ptr)
    if (UnaryOperator* unary = dyn_cast<UnaryOperator>(expr)) {
        if (unary->getOpcode() == UO_Deref) {
            Expr* ptr_expr = unary->getSubExpr()->IgnoreImpCasts();

            // 检查指针是否指向全局变量
            if (DeclRefExpr* ptr_decl = dyn_cast<DeclRefExpr>(ptr_expr)) {
                std::string ptr_name = ptr_decl->getDecl()->getNameAsString();

                // 检查是否是指向全局变量的指针别名
                std::string full_alias = CurrentFile + "::" + CurrentFunction + "::" + ptr_name;
                std::string global_path = Data->getGlobalAlias(full_alias);
                if (!global_path.empty()) {
                    return true;
                }

                // 检查指针本身是否是全局变量
                if (VarDecl* ptr_var_decl = dyn_cast<VarDecl>(ptr_decl->getDecl())) {
                    if (isGlobalVariableDecl(ptr_var_decl)) {
                        return true;
                    }
                }
            }
        }
    }

    // 4. 数组访问 (arr[i])
    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        return isGlobalVariableOrIndirect(array->getBase(), "");
    }

    return false;
}

std::string WriteAnalyzer::extractWriteTarget(Expr* expr) {
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
            // 对于解引用，我们需要获取被解引用的变量名
            Expr* sub_expr = unary->getSubExpr()->IgnoreImpCasts();
            if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(sub_expr)) {
                return decl_ref->getDecl()->getNameAsString();
            }
            return extractWriteTarget(sub_expr);
        }
    }

    if (ArraySubscriptExpr* array = dyn_cast<ArraySubscriptExpr>(expr)) {
        std::string base = extractWriteTarget(array->getBase());
        return base + "[...]";
    }

    return "unknown";
}

//=============================================================================
// 写操作分类
//=============================================================================

std::string WriteAnalyzer::classifyWriteOperation(Expr* expr, const std::string& target) {
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

    // 分析直接的变量引用
    if (DeclRefExpr* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
        QualType var_type = decl_ref->getType();

        // 如果是指针类型，检查指向的类型
        if (var_type->isPointerType()) {
            QualType pointee_type = var_type->getPointeeType();

            // 检查指向的是否是结构体类型
            if (pointee_type->isStructureType() || pointee_type->isUnionType()) {
                return "data_structure";
            }

            // 对于指向基本类型的指针，根据变量名和上下文判断
            std::string var_name = decl_ref->getDecl()->getNameAsString();
            return classifyVariableByName(var_name, pointee_type);
        }

        // 检查是否是结构体或联合体类型
        if (var_type->isStructureType() || var_type->isUnionType()) {
            return "data_structure";
        }
    }

    // 普通变量赋值
    return "variable";
}

std::string WriteAnalyzer::classifyVariableByName(const std::string& var_name, QualType pointee_type) {
    std::string lower_name = var_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);

    // 检查是否是链表相关的变量名
    if (lower_name.find("list") != std::string::npos ||
        lower_name.find("head") != std::string::npos ||
        lower_name.find("node") != std::string::npos ||
        lower_name.find("queue") != std::string::npos) {
        return "data_structure";
    }

    // 检查是否是设备或驱动相关
    if (lower_name.find("dev") != std::string::npos ||
        lower_name.find("device") != std::string::npos ||
        lower_name.find("driver") != std::string::npos ||
        lower_name.find("ctrl") != std::string::npos ||
        lower_name.find("config") != std::string::npos) {
        return "data_structure";
    }

    // 检查是否是缓冲区相关
    if (lower_name.find("buf") != std::string::npos ||
        lower_name.find("buffer") != std::string::npos ||
        lower_name.find("data") != std::string::npos) {
        return "variable";
    }

    // 根据指向的类型名称判断
    if (!pointee_type.isNull()) {
        std::string type_name = pointee_type.getAsString();
        std::transform(type_name.begin(), type_name.end(), type_name.begin(), ::tolower);

        if (type_name.find("struct") != std::string::npos ||
            type_name.find("union") != std::string::npos ||
            type_name.find("list_head") != std::string::npos) {
            return "data_structure";
        }
    }

    // 默认分类为变量
    return "variable";
}

std::string WriteAnalyzer::classifyPointerTarget(Expr* ptr_expr) {
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

//=============================================================================
// 寄存器检测
//=============================================================================

bool WriteAnalyzer::isRegisterRelatedVariable(const std::string& target) {
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

bool WriteAnalyzer::isExactRegisterName(const std::string& lower_name) {
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

bool WriteAnalyzer::isRegisterNamingPattern(const std::string& lower_name) {
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
// 私有辅助方法
//=============================================================================

std::vector<std::string> WriteAnalyzer::collectModifiedFields(FunctionDecl* callee, ParmVarDecl* param) {
    std::vector<std::string> result;
    if (!callee || !param || !callee->hasBody()) return result;

    // 修复后的参数字段访问器 - 添加正确的头文件引用
    class ParamFieldVisitor : public clang::RecursiveASTVisitor<ParamFieldVisitor> {
    public:
        WriteAnalyzer* Parent;
        ParmVarDecl* Param;
        std::vector<std::string>& Res;

        ParamFieldVisitor(WriteAnalyzer* P, ParmVarDecl* Prm, std::vector<std::string>& R)
            : Parent(P), Param(Prm), Res(R) {}

        bool isParamAccess(Expr* expr) {
            if (!expr) return false;
            expr = expr->IgnoreImpCasts();
            if (DeclRefExpr* dr = dyn_cast<DeclRefExpr>(expr)) {
                return dr->getDecl() == Param;
            }
            if (MemberExpr* me = dyn_cast<MemberExpr>(expr)) {
                return isParamAccess(me->getBase());
            }
            if (ArraySubscriptExpr* arr = dyn_cast<ArraySubscriptExpr>(expr)) {
                return isParamAccess(arr->getBase());
            }
            if (UnaryOperator* un = dyn_cast<UnaryOperator>(expr)) {
                if (un->getOpcode() == UO_Deref) {
                    return isParamAccess(un->getSubExpr());
                }
            }
            return false;
        }

        bool VisitBinaryOperator(BinaryOperator* op) {
            if (!op->isAssignmentOp()) return true;
            Expr* lhs = op->getLHS();
            if (isParamAccess(lhs)) {
                std::string target = Parent->extractWriteTarget(lhs);
                Res.push_back(target);
            }
            return true;
        }

        bool VisitUnaryOperator(UnaryOperator* op) {
            auto opc = op->getOpcode();
            if (opc == UO_PostInc || opc == UO_PreInc ||
                opc == UO_PostDec || opc == UO_PreDec) {
                Expr* sub = op->getSubExpr();
                if (isParamAccess(sub)) {
                    std::string target = Parent->extractWriteTarget(sub);
                    Res.push_back(target);
                }
            }
            return true;
        }
    };

    ParamFieldVisitor visitor(this, param, result);
    visitor.TraverseStmt(callee->getBody());

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    return result;
}

std::string WriteAnalyzer::analyzePointerContext(Expr* ptr_expr, QualType pointee_type) {
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

std::string WriteAnalyzer::classifyByNamingConvention(const std::string& ptr_name, QualType pointee_type) {
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

std::string WriteAnalyzer::analyzeGlobalPathType(const std::string& global_path) {
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
    std::string base_name = global_path;
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
    
    return classifyByNamingConvention(base_name, QualType());
}

bool WriteAnalyzer::isGlobalVariableDecl(VarDecl* var_decl) {
    return var_decl->hasGlobalStorage() ||
           var_decl->getStorageClass() == SC_Static ||
           var_decl->hasExternalFormalLinkage();
}
