#include "ast_visitor.h"
#include <clang/AST/AST.h>
#include <clang/Basic/SourceManager.h>

using namespace clang;

InterruptAnalysisVisitor::InterruptAnalysisVisitor(ASTContext* context, AnalysisData* data, const std::string& file)
    : InterruptAnalysisVisitorBase(context, data, file) {
    initializeAnalyzers();
}

//=============================================================================
// AST节点访问方法重写
//=============================================================================

bool InterruptAnalysisVisitor::VisitVarDecl(VarDecl* var_decl) {
    // 调用基类方法处理基础功能
    InterruptAnalysisVisitorBase::VisitVarDecl(var_decl);
    
    // 如果是局部变量且有初始化，分析指针别名
    if (!CurrentFunction.empty() && var_decl->hasInit()) {
        updateAnalyzersContext();
        pointer_analyzer->analyzePointerAlias(var_decl);
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

    updateAnalyzersContext();

    if (FunctionDecl* callee = call->getDirectCallee()) {
        std::string callee_name = callee->getNameAsString();
        
        // 防止无限递归
        if (!isRecursiveCall(callee_name)) {
            enterFunction(callee_name);
            
            recordDirectCall(callee_name, call, line, column);
            
            // 多层调用分析
            pointer_analyzer->analyzeMultiLevelCall(call, callee);
            
            // 函数指针作为参数的分析
            pointer_analyzer->analyzeFunctionPointerAsParameter(call, callee);
            
            // 参数传播分析
            pointer_analyzer->analyzeParameterPropagation(call, callee);
            
            // 函数参数分析
            write_analyzer->analyzeFunctionArguments(call, callee);
            
            exitFunction(callee_name);
        }
    }
    else {
        // 间接函数调用（函数指针）
        fp_analyzer->analyzeFunctionPointerCall(call);
    }

    return true;
}

bool InterruptAnalysisVisitor::VisitBinaryOperator(BinaryOperator* op) {
    if (CurrentFunction.empty()) return true;
    if (!op->isAssignmentOp()) return true;

    Expr* lhs = op->getLHS();
    Expr* rhs = op->getRHS();
    
    updateAnalyzersContext();

    // 检查函数指针赋值
    if (lhs->getType()->isFunctionPointerType()) {
        fp_analyzer->analyzeFunctionPointerAssignment(lhs, rhs, "direct", op);
    }

    // 检查所有写操作，包括对全局变量的间接写入
    write_analyzer->analyzeWriteOperation(lhs, op, "BinaryOperator");

    return true;
}

bool InterruptAnalysisVisitor::VisitUnaryOperator(UnaryOperator* op) {
    if (CurrentFunction.empty()) return true;

    UnaryOperator::Opcode opcode = op->getOpcode();
    if (opcode == UO_PostInc || opcode == UO_PreInc ||
        opcode == UO_PostDec || opcode == UO_PreDec) {
        
        updateAnalyzersContext();
        write_analyzer->analyzeWriteOperation(op->getSubExpr(), op, "UnaryOperator");
    }

    return true;
}

bool InterruptAnalysisVisitor::VisitInitListExpr(InitListExpr* init_list) {
    if (CurrentFunction.empty()) return true;

    updateAnalyzersContext();
    fp_analyzer->analyzeFunctionPointerInInitList(init_list);
    return true;
}

bool InterruptAnalysisVisitor::VisitDesignatedInitExpr(DesignatedInitExpr* designated_init) {
    if (CurrentFunction.empty()) return true;

    updateAnalyzersContext();
    fp_analyzer->analyzeFunctionPointerInDesignatedInit(designated_init);
    return true;
}

bool InterruptAnalysisVisitor::VisitGCCAsmStmt(GCCAsmStmt* asm_stmt) {
    if (CurrentFunction.empty()) return true;

    updateAnalyzersContext();
    asm_analyzer->analyzeInlineAssembly(asm_stmt);
    return true;
}

bool InterruptAnalysisVisitor::VisitReturnStmt(ReturnStmt* ret_stmt) {
    if (CurrentFunction.empty()) return true;
    
    updateAnalyzersContext();
    pointer_analyzer->analyzeReturnValue(ret_stmt);
    return true;
}

//=============================================================================
// 私有方法
//=============================================================================

void InterruptAnalysisVisitor::initializeAnalyzers() {
    // 创建分析模块实例
    pointer_analyzer = std::make_unique<PointerAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
    write_analyzer = std::make_unique<WriteAnalyzer>(Context, Data, pointer_analyzer.get(), CurrentFile, CurrentFunction);
    fp_analyzer = std::make_unique<FunctionPointerAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
    asm_analyzer = std::make_unique<AssemblyAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
}

void InterruptAnalysisVisitor::updateAnalyzersContext() {
    // 更新所有分析器的上下文（当前函数可能已经改变）
    pointer_analyzer = std::make_unique<PointerAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
    write_analyzer = std::make_unique<WriteAnalyzer>(Context, Data, pointer_analyzer.get(), CurrentFile, CurrentFunction);
    fp_analyzer = std::make_unique<FunctionPointerAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
    asm_analyzer = std::make_unique<AssemblyAnalyzer>(Context, Data, CurrentFile, CurrentFunction);
}
