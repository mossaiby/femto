#include "ast_to_hir.h"
#include "sema/type_resolver.h"

#include <unordered_map>

namespace femto::hir {

ASTToHIR::ASTToHIR(DiagnosticEngine& diag) : diag_(diag) {}

HIRModule ASTToHIR::lower(ast::Module& mod) {
    HIRModule hir;
    hir.filename = mod.filename;

    // Build function return type lookup table from AST declarations
    for (auto& decl : mod.decls) {
        if (!decl) continue;
        if (auto* func = std::get_if<ast::FunctionDecl>(&decl->data)) {
            auto ret_type = func->return_type ? femto::sema::resolve_ast_type(*func->return_type) : nullptr;
            func_return_types_[func->name] = ret_type;
        } else if (auto* ns = std::get_if<ast::NamespaceDecl>(&decl->data)) {
            for (auto& ns_decl : ns->decls) {
                if (!ns_decl) continue;
                if (auto* func = std::get_if<ast::FunctionDecl>(&ns_decl->data)) {
                    auto ret_type = func->return_type ? femto::sema::resolve_ast_type(*func->return_type) : nullptr;
                    func_return_types_[ns->name + "." + func->name] = ret_type;
                }
            }
        }
    }

    for (auto& decl : mod.decls) {
        if (decl) {
            auto hd = lower_decl(*decl);
            if (hd) hir.decls.push_back(std::move(hd));
        }
    }
    return hir;
}

DeclPtr ASTToHIR::lower_decl(ast::Decl& decl) {
    return std::visit([this, &decl](auto& d) -> DeclPtr {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, ast::FunctionDecl>) {
            auto h = std::make_unique<HIRDecl>();
            h->span = decl.span;
            h->data = lower_function(d);
            return h;
        } else if constexpr (std::is_same_v<T, ast::StructDecl>) {
            auto h = std::make_unique<HIRDecl>();
            h->span = decl.span;
            h->data = lower_struct(d);
            return h;
        } else if constexpr (std::is_same_v<T, ast::EnumDecl>) {
            auto h = std::make_unique<HIRDecl>();
            h->span = decl.span;
            h->data = lower_enum(d);
            return h;
        } else if constexpr (std::is_same_v<T, ast::ConstantDecl>) {
            auto h = std::make_unique<HIRDecl>();
            h->span = decl.span;
            h->data = lower_constant(d);
            return h;
        }
        return nullptr;
    }, decl.data);
}

HIRFunction ASTToHIR::lower_function(ast::FunctionDecl& func) {
    HIRFunction h;
    h.name = func.name;
    h.is_error_return = func.is_error_return;
    // Type info comes from sema; resolve AST types to sema types
    h.return_type = func.return_type ? femto::sema::resolve_ast_type(*func.return_type) : nullptr;
    for (auto& p : func.params) {
        auto param_type = p.type ? femto::sema::resolve_ast_type(*p.type) : nullptr;
        h.params.push_back({p.name, param_type});
    }
    if (func.body) {
        h.body = lower_block(std::get<ast::Block>(func.body->data));
    }
    return h;
}

HIRStruct ASTToHIR::lower_struct(ast::StructDecl& str) {
    HIRStruct h;
    h.name = str.name;
    for (auto& f : str.fields) {
        h.fields.push_back({f.name, nullptr});
    }
    return h;
}

HIREnum ASTToHIR::lower_enum(ast::EnumDecl& en) {
    HIREnum h;
    h.name = en.name;
    std::int64_t next_val = 0;
    for (auto& v : en.variants) {
        std::int64_t val = next_val;
        if (v.value) {
            if (auto* lit = std::get_if<ast::IntegerLit>(&(*v.value)->data)) {
                val = lit->value;
            }
        }
        h.variants.push_back({v.name, val});
        next_val = val + 1;
    }
    return h;
}

HIRConstant ASTToHIR::lower_constant(ast::ConstantDecl& con) {
    HIRConstant h;
    h.name = con.name;
    if (con.init) h.init = lower_expr(*con.init);
    return h;
}

std::unique_ptr<HIRBlock> ASTToHIR::lower_block(ast::Block& block) {
    auto hb = std::make_unique<HIRBlock>();
    for (auto& stmt : block.stmts) {
        auto hs = lower_stmt(*stmt);
        if (hs) hb->stmts.push_back(std::move(hs));
    }
    return hb;
}

StmtPtr ASTToHIR::lower_stmt(ast::Stmt& stmt) {
    return std::visit([this, &stmt](auto& s) -> StmtPtr {
        using T = std::decay_t<decltype(s)>;
        auto hs = std::make_unique<HIRStmt>();
        hs->span = stmt.span;

        if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            hs->data = ExprStmt{lower_expr(*s.expr)};
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            std::string target_name;
            if (auto* id = std::get_if<ast::Identifier>(&s.target->data)) {
                target_name = id->name;
            } else if (auto* me = std::get_if<ast::MemberExpr>(&s.target->data)) {
                target_name = me->member;
            }
            hs->data = AssignStmt{target_name, lower_expr(*s.value)};
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            std::optional<ExprPtr> val;
            if (s.value) val = lower_expr(**s.value);
            hs->data = ReturnStmt{std::move(val)};
        } else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            auto cond = lower_expr(*s.condition);
            auto then_blk = lower_block(std::get<ast::Block>(s.then_block->data));
            std::unique_ptr<HIRBlock> else_blk;
            if (s.else_block) {
                // else_block can be either a Block (for final else) or an IfStmt (for else if chains)
                auto& else_stmt = **s.else_block;
                if (auto* block = std::get_if<ast::Block>(&else_stmt.data)) {
                    else_blk = lower_block(*block);
                } else if (auto* if_stmt = std::get_if<ast::IfStmt>(&else_stmt.data)) {
                    // Recursively lower the else-if chain
                    auto else_cond = lower_expr(*if_stmt->condition);
                    auto else_then = lower_block(std::get<ast::Block>(if_stmt->then_block->data));
                    std::unique_ptr<HIRBlock> else_else;
                    if (if_stmt->else_block) {
                        auto& nested_else = **if_stmt->else_block;
                        if (auto* nested_block = std::get_if<ast::Block>(&nested_else.data)) {
                            else_else = lower_block(*nested_block);
                        } else if (auto* nested_if = std::get_if<ast::IfStmt>(&nested_else.data)) {
                            // This shouldn't happen with our iterative parser, but handle it
                            else_else = lower_block(std::get<ast::Block>(nested_if->then_block->data));
                        }
                    }
                    // Create a nested IfStmt in HIR
                    auto nested_if = std::make_unique<HIRStmt>();
                    nested_if->span = else_stmt.span;
                    nested_if->data = IfStmt{std::move(else_cond), std::move(else_then), std::move(else_else)};
                    else_blk = std::make_unique<HIRBlock>();
                    else_blk->stmts.push_back(std::move(nested_if));
                }
            }
            hs->data = IfStmt{std::move(cond), std::move(then_blk), std::move(else_blk)};
        } else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
            auto cond = lower_expr(*s.condition);
            auto body = lower_block(std::get<ast::Block>(s.body->data));
            hs->data = WhileStmt{std::move(cond), std::move(body)};
        } else if constexpr (std::is_same_v<T, ast::DeclPtr>) {
            if (s) {
                auto* var_decl = std::get_if<ast::VariableDecl>(&s->data);
                if (var_decl && var_decl->init) {
                    hs->data = AssignStmt{var_decl->name, lower_expr(*var_decl->init)};
                } else {
                    return nullptr;
                }
            } else {
                return nullptr;
            }
        } else if constexpr (std::is_same_v<T, ast::Block>) {
            auto blk = std::make_unique<HIRBlock>();
            for (auto& st : s.stmts) {
                auto inner = lower_stmt(*st);
                if (inner) blk->stmts.push_back(std::move(inner));
            }
            hs->data = Block{std::move(blk->stmts)};
        } else if constexpr (std::is_same_v<T, ast::BreakStmt> || std::is_same_v<T, ast::ContinueStmt>) {
            return nullptr;
        } else {
            return nullptr;
        }
        return hs;
    }, stmt.data);
}

ExprPtr ASTToHIR::lower_expr(ast::Expr& expr) {
    return std::visit([this, &expr](auto& e) -> ExprPtr {
        using T = std::decay_t<decltype(e)>;
        auto he = std::make_unique<HIRExpr>();
        he->span = expr.span;

        if constexpr (std::is_same_v<T, ast::IntegerLit>) {
            he->data = IntLit{e.value, femto::sema::make_int(64)};
        } else if constexpr (std::is_same_v<T, ast::FloatLit>) {
            he->data = FloatLit{e.value, femto::sema::make_float(64)};
        } else if constexpr (std::is_same_v<T, ast::StringLit>) {
            he->data = StringLit{e.value, femto::sema::make_string(8)};
        } else if constexpr (std::is_same_v<T, ast::BoolLit>) {
            he->data = BoolLit{e.value, femto::sema::make_bool(8)};
        } else if constexpr (std::is_same_v<T, ast::NullLit>) {
            he->data = NullLit{nullptr};
        } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            he->data = VarRef{e.name, nullptr};
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            BinaryOp::Op op;
            switch (e.op) {
                case ast::BinaryExpr::Add: op = BinaryOp::Add; break;
                case ast::BinaryExpr::Sub: op = BinaryOp::Sub; break;
                case ast::BinaryExpr::Mul: op = BinaryOp::Mul; break;
                case ast::BinaryExpr::Div: op = BinaryOp::Div; break;
                case ast::BinaryExpr::Mod: op = BinaryOp::Mod; break;
                case ast::BinaryExpr::Eq: op = BinaryOp::Eq; break;
                case ast::BinaryExpr::Neq: op = BinaryOp::Neq; break;
                case ast::BinaryExpr::Lt: op = BinaryOp::Lt; break;
                case ast::BinaryExpr::Gt: op = BinaryOp::Gt; break;
                case ast::BinaryExpr::Le: op = BinaryOp::Le; break;
                case ast::BinaryExpr::Ge: op = BinaryOp::Ge; break;
                case ast::BinaryExpr::BitAnd: op = BinaryOp::BitAnd; break;
                case ast::BinaryExpr::BitOr: op = BinaryOp::BitOr; break;
                case ast::BinaryExpr::BitXor: op = BinaryOp::BitXor; break;
                case ast::BinaryExpr::LShift: op = BinaryOp::LShift; break;
                case ast::BinaryExpr::RShift: op = BinaryOp::RShift; break;
                case ast::BinaryExpr::LogicAnd: op = BinaryOp::LogicAnd; break;
                case ast::BinaryExpr::LogicOr: op = BinaryOp::LogicOr; break;
                default: op = BinaryOp::Add; break;
            }
            {
                auto left_e = lower_expr(*e.left);
                auto right_e = lower_expr(*e.right);
                BinaryOp bin{op, std::move(left_e), std::move(right_e), nullptr};
                he->data = std::move(bin);
            }
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            UnaryOp::Op op;
            switch (e.op) {
                case ast::UnaryExpr::Neg: op = UnaryOp::Neg; break;
                case ast::UnaryExpr::Not: op = UnaryOp::Not; break;
                case ast::UnaryExpr::BitNot: op = UnaryOp::BitNot; break;
                case ast::UnaryExpr::Deref: op = UnaryOp::Deref; break;
                case ast::UnaryExpr::Addr: op = UnaryOp::Addr; break;
                default: op = UnaryOp::Neg; break;
            }
            {
                auto operand_e = lower_expr(*e.operand);
                UnaryOp uo{op, std::move(operand_e), nullptr};
                he->data = std::move(uo);
            }
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            std::string callee_name;
            if (auto* id = std::get_if<ast::Identifier>(&e.callee->data)) {
                callee_name = id->name;
            } else if (auto* me = std::get_if<ast::MemberExpr>(&e.callee->data)) {
                // Build qualified name: walk the member chain
                std::string name = me->member;
                ast::Expr* obj = me->object.get();
                while (true) {
                    if (auto* inner_id = std::get_if<ast::Identifier>(&obj->data)) {
                        name = inner_id->name + "." + name;
                        break;
                    } else if (auto* inner_me = std::get_if<ast::MemberExpr>(&obj->data)) {
                        name = inner_me->member + "." + name;
                        obj = inner_me->object.get();
                    } else {
                        break;
                    }
                }
                callee_name = name;
            }
            std::vector<ExprPtr> args;
            for (auto& a : e.args) args.push_back(lower_expr(*a));
            // Look up return type from function declarations
            femto::sema::TypePtr ret_type = nullptr;
            auto it = func_return_types_.find(callee_name);
            if (it != func_return_types_.end()) {
                ret_type = it->second;
            }
            he->data = CallOp{callee_name, std::move(args), ret_type};
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            he->data = CastOp{lower_expr(*e.value), nullptr};
        } else if constexpr (std::is_same_v<T, ast::MemberExpr>) {
            he->data = MemberAccess{lower_expr(*e.object), e.member, nullptr};
        } else if constexpr (std::is_same_v<T, ast::IndexExpr>) {
            auto obj_e = lower_expr(*e.object);
            auto idx_e = lower_expr(*e.index);
            IndexOp io{std::move(obj_e), std::move(idx_e), nullptr};
            he->data = std::move(io);
        } else if constexpr (std::is_same_v<T, ast::SuccessExpr>) {
            std::vector<ExprPtr> args;
            args.push_back(lower_expr(*e.value));
            CallOp co{"success", std::move(args), nullptr};
            he->data = std::move(co);
        } else if constexpr (std::is_same_v<T, ast::FailureExpr>) {
            std::vector<ExprPtr> args;
            if (e.value) args.push_back(lower_expr(**e.value));
            he->data = CallOp{"failure", std::move(args), nullptr};
        } else if constexpr (std::is_same_v<T, ast::StructLiteral>) {
            // Lower struct literal fields
            for (auto& [name, val] : e.fields) {
                lower_expr(*val);
            }
            he->data = NullLit{nullptr};
        } else {
            he->data = IntLit{0, nullptr};
        }
        return he;
    }, expr.data);
}

} // namespace femto::hir
