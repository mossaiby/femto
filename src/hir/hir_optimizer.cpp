#include "hir_optimizer.h"

namespace femto::hir {

void HIROptimizer::optimize(HIRModule& mod) {
    for (auto& decl : mod.decls) {
        if (decl) optimize_decl(*decl);
    }
}

void HIROptimizer::optimize_decl(HIRDecl& decl) {
    std::visit([this](auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, HIRFunction>) optimize_function(d);
    }, decl.data);
}

void HIROptimizer::optimize_function(HIRFunction& func) {
    if (func.body) optimize_block(*func.body);
}

void HIROptimizer::optimize_block(HIRBlock& block) {
    for (auto& stmt : block.stmts) {
        optimize_stmt(*stmt);
    }
}

void HIROptimizer::optimize_stmt(HIRStmt& stmt) {
    std::visit([this](auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, AssignStmt>) {
            s.value = optimize_expr(std::move(s.value));
        } else if constexpr (std::is_same_v<T, ExprStmt>) {
            s.expr = optimize_expr(std::move(s.expr));
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            if (s.value) s.value = optimize_expr(std::move(*s.value));
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            s.condition = optimize_expr(std::move(s.condition));
            if (s.then_block) optimize_block(*s.then_block);
            if (s.else_block) optimize_block(*s.else_block);
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            s.condition = optimize_expr(std::move(s.condition));
            if (s.body) optimize_block(*s.body);
        } else if constexpr (std::is_same_v<T, Block>) {
            for (auto& st : s.stmts) optimize_stmt(*st);
        }
    }, stmt.data);
}

ExprPtr HIROptimizer::optimize_expr(ExprPtr expr) {
    if (!expr) return expr;

    // Optimize children first based on type
    if (auto* binop = std::get_if<BinaryOp>(&expr->data)) {
        binop->left = optimize_expr(std::move(binop->left));
        binop->right = optimize_expr(std::move(binop->right));
        auto folded = try_fold_binary(*binop, std::move(binop->left), std::move(binop->right));
        return folded;
    }
    if (auto* unop = std::get_if<UnaryOp>(&expr->data)) {
        unop->operand = optimize_expr(std::move(unop->operand));
        auto folded = try_fold_unary(*unop, std::move(unop->operand));
        return folded;
    }
    return expr;
}

ExprPtr HIROptimizer::try_fold_binary(BinaryOp& op, ExprPtr left, ExprPtr right) {
    std::int64_t li, ri;
    double lf, rf;

    auto result = std::make_unique<HIRExpr>();
    result->span = {};

    if (is_constant_int(*left, li) && is_constant_int(*right, ri)) {
        std::int64_t folded;
        bool folded_ok = true;
        switch (op.op) {
            case BinaryOp::Add: folded = li + ri; break;
            case BinaryOp::Sub: folded = li - ri; break;
            case BinaryOp::Mul: folded = li * ri; break;
            case BinaryOp::Div: folded = ri != 0 ? li / ri : 0; break;
            case BinaryOp::Mod: folded = ri != 0 ? li % ri : 0; break;
            case BinaryOp::Eq: folded = li == ri; break;
            case BinaryOp::Neq: folded = li != ri; break;
            case BinaryOp::Lt: folded = li < ri; break;
            case BinaryOp::Gt: folded = li > ri; break;
            case BinaryOp::Le: folded = li <= ri; break;
            case BinaryOp::Ge: folded = li >= ri; break;
            case BinaryOp::BitAnd: folded = li & ri; break;
            case BinaryOp::BitOr: folded = li | ri; break;
            case BinaryOp::BitXor: folded = li ^ ri; break;
            case BinaryOp::LShift: folded = li << ri; break;
            case BinaryOp::RShift: folded = li >> ri; break;
            case BinaryOp::LogicAnd: folded = li && ri; break;
            case BinaryOp::LogicOr: folded = li || ri; break;
            default: folded_ok = false; break;
        }
        if (folded_ok) {
            result->data = IntLit{folded, op.type};
            return result;
        }
    }

    if (is_constant_float(*left, lf) && is_constant_float(*right, rf)) {
        double folded;
        bool folded_ok = true;
        switch (op.op) {
            case BinaryOp::Add: folded = lf + rf; break;
            case BinaryOp::Sub: folded = lf - rf; break;
            case BinaryOp::Mul: folded = lf * rf; break;
            case BinaryOp::Div: folded = rf != 0.0 ? lf / rf : 0.0; break;
            default: folded_ok = false; break;
        }
        if (folded_ok) {
            result->data = FloatLit{folded, op.type};
            return result;
        }
    }

    result->data = BinaryOp{op.op, std::move(left), std::move(right), op.type};
    return result;
}

ExprPtr HIROptimizer::try_fold_unary(UnaryOp& op, ExprPtr operand) {
    std::int64_t val;
    if (is_constant_int(*operand, val)) {
        auto result = std::make_unique<HIRExpr>();
        result->span = {};
        switch (op.op) {
            case UnaryOp::Neg: result->data = IntLit{-val, op.type}; break;
            case UnaryOp::Not: result->data = BoolLit{val == 0, op.type}; break;
            case UnaryOp::BitNot: result->data = IntLit{~val, op.type}; break;
            default: result->data = UnaryOp{op.op, std::move(operand), op.type}; break;
        }
        return result;
    }

    auto result = std::make_unique<HIRExpr>();
    result->span = {};
    result->data = UnaryOp{op.op, std::move(operand), op.type};
    return result;
}

bool HIROptimizer::is_constant_int(const HIRExpr& expr, std::int64_t& out) {
    if (auto* lit = std::get_if<IntLit>(&expr.data)) { out = lit->value; return true; }
    if (auto* blit = std::get_if<BoolLit>(&expr.data)) { out = blit->value ? 1 : 0; return true; }
    return false;
}

bool HIROptimizer::is_constant_float(const HIRExpr& expr, double& out) {
    if (auto* lit = std::get_if<FloatLit>(&expr.data)) { out = lit->value; return true; }
    return false;
}

} // namespace femto::hir
