#ifndef AST_VISITOR_H
#define AST_VISITOR_H

#include "ast_visitor_base.h"
#include "pointer_analysis.h"
#include "write_analysis.h"
#include "function_pointer_analysis.h"
#include "assembly_analysis.h"
#include <memory>

/**
 * 完整的中断分析AST访问器
 * 继承基础访问器并集成各种分析模块
 */
class InterruptAnalysisVisitor : public InterruptAnalysisVisitorBase {
private:
    // 分析模块
    std::unique_ptr<PointerAnalyzer> pointer_analyzer;
    std::unique_ptr<WriteAnalyzer> write_analyzer;
    std::unique_ptr<FunctionPointerAnalyzer> fp_analyzer;
    std::unique_ptr<AssemblyAnalyzer> asm_analyzer;

public:
    explicit InterruptAnalysisVisitor(clang::ASTContext* context, AnalysisData* data, const std::string& file);
    
    // 重写父类的访问方法
    bool VisitVarDecl(clang::VarDecl* var_decl) override;
    bool VisitCallExpr(clang::CallExpr* call) override;
    bool VisitBinaryOperator(clang::BinaryOperator* op) override;
    bool VisitUnaryOperator(clang::UnaryOperator* op) override;
    bool VisitInitListExpr(clang::InitListExpr* init_list) override;
    bool VisitDesignatedInitExpr(clang::DesignatedInitExpr* designated_init) override;
    bool VisitGCCAsmStmt(clang::GCCAsmStmt* asm_stmt) override;
    bool VisitReturnStmt(clang::ReturnStmt* ret_stmt) override;

private:
    // 初始化分析模块
    void initializeAnalyzers();
    
    // 更新分析器的当前上下文
    void updateAnalyzersContext();
};

#endif // AST_VISITOR_H
