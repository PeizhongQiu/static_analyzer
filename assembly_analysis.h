#ifndef ASSEMBLY_ANALYSIS_H
#define ASSEMBLY_ANALYSIS_H

#include "analysis_data.h"
#include <clang/AST/AST.h>
#include <string>
#include <vector>
#include <regex>

/**
 * 汇编代码分析模块
 * 处理内联汇编和寄存器操作检测
 */
class AssemblyAnalyzer {
private:
    clang::ASTContext* Context;
    AnalysisData* Data;
    std::string CurrentFile;
    std::string CurrentFunction;

public:
    explicit AssemblyAnalyzer(clang::ASTContext* context, AnalysisData* data, 
                            const std::string& file, const std::string& function);

    // 内联汇编分析
    void analyzeInlineAssembly(clang::GCCAsmStmt* asm_stmt);
    
    // 寄存器操作解析
    RegisterOperation parseRegisterOperation(const std::smatch& match, const std::string& op_info,
                                           clang::GCCAsmStmt* asm_stmt, const std::string& asm_text);

private:
    // 寄存器操作模式
    std::vector<std::pair<std::regex, std::string>> getRegisterPatterns();
    
    // 操作类型解析
    void parseOperationType(RegisterOperation& reg_op, const std::smatch& match, const std::string& op_info);
};

#endif // ASSEMBLY_ANALYSIS_H
