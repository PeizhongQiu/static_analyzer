#include "assembly_analysis.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>
#include <regex>
#include <iostream>

using namespace clang;

AssemblyAnalyzer::AssemblyAnalyzer(ASTContext* context, AnalysisData* data, 
                                 const std::string& file, const std::string& function)
    : Context(context), Data(data), CurrentFile(file), CurrentFunction(function) {}

//=============================================================================
// 内联汇编分析
//=============================================================================

void AssemblyAnalyzer::analyzeInlineAssembly(GCCAsmStmt* asm_stmt) {
    StringLiteral* asm_str = asm_stmt->getAsmString();
    if (!asm_str) return;

    std::string asm_text = asm_str->getString().str();

    // 获取寄存器操作检测模式
    auto patterns = getRegisterPatterns();

    for (const auto& [pattern, op_info] : patterns) {
        std::sregex_iterator iter(asm_text.begin(), asm_text.end(), pattern);
        std::sregex_iterator end;

        for (; iter != end; ++iter) {
            RegisterOperation reg_op = parseRegisterOperation(*iter, op_info, asm_stmt, asm_text);
            Data->addRegisterOp(reg_op);
        }
    }
}

RegisterOperation AssemblyAnalyzer::parseRegisterOperation(const std::smatch& match,
                                                         const std::string& op_info,
                                                         GCCAsmStmt* asm_stmt,
                                                         const std::string& asm_text) {
    RegisterOperation reg_op;
    reg_op.function = CurrentFunction;
    reg_op.file = CurrentFile;
    reg_op.details = asm_text;

    // 解析操作类型和目标
    parseOperationType(reg_op, match, op_info);

    SourceLocation loc = asm_stmt->getBeginLoc();
    if (loc.isValid()) {
        reg_op.line = Context->getSourceManager().getSpellingLineNumber(loc);
        reg_op.column = Context->getSourceManager().getSpellingColumnNumber(loc);
    }

    reg_op.node_id = "asm_" + std::to_string(reinterpret_cast<uintptr_t>(asm_stmt));

    return reg_op;
}

//=============================================================================
// 私有方法
//=============================================================================

std::vector<std::pair<std::regex, std::string>> AssemblyAnalyzer::getRegisterPatterns() {
    return {
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
}

void AssemblyAnalyzer::parseOperationType(RegisterOperation& reg_op, const std::smatch& match, const std::string& op_info) {
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
}
