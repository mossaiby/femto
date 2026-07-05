#pragma once

#include "hir.h"

namespace femto::hir {

class HIROptimizer {
public:
    void optimize(HIRModule& mod);

private:
    void optimize_decl(HIRDecl& decl);
    void optimize_function(HIRFunction& func);
    void optimize_block(HIRBlock& block);
    void optimize_stmt(HIRStmt& stmt);
    ExprPtr optimize_expr(ExprPtr expr);

    // Constant folding
    ExprPtr try_fold_binary(BinaryOp& op, ExprPtr left, ExprPtr right);
    ExprPtr try_fold_unary(UnaryOp& op, ExprPtr operand);

    bool is_constant_int(const HIRExpr& expr, std::int64_t& out);
    bool is_constant_float(const HIRExpr& expr, double& out);
};

} // namespace femto::hir
