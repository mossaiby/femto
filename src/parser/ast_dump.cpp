#include "ast.h"
#include <iostream>
#include <string>

namespace femto::ast {

static void dump_indent(int indent) {
    for (int i = 0; i < indent; ++i) std::cout << "  ";
}

void dump_type(const TypeNode& node, int indent = 0) {
    dump_indent(indent);
    std::cout << "TypeNode: ";
    std::visit([&](const auto& t) {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, PrimitiveType>) {
            std::cout << "Primitive(" << static_cast<int>(t.token_type) << ")";
        } else if constexpr (std::is_same_v<T, PointerType>) {
            std::cout << "Pointer\n";
            dump_type(*t.inner, indent + 1);
        } else if constexpr (std::is_same_v<T, SliceType>) {
            std::cout << "Slice\n";
            dump_type(*t.inner, indent + 1);
        } else if constexpr (std::is_same_v<T, ArrayType>) {
            std::cout << "Array\n";
            dump_type(*t.inner, indent + 1);
        } else if constexpr (std::is_same_v<T, FunctionType>) {
            std::cout << "Function(" << t.param_types.size() << " params, error_return=" << t.is_error_return << ")\n";
            for (auto& p : t.param_types) dump_type(*p, indent + 1);
            dump_type(*t.return_type, indent + 1);
        } else if constexpr (std::is_same_v<T, NamedType>) {
            std::cout << "Named(" << t.name << ")";
        } else if constexpr (std::is_same_v<T, GenericType>) {
            std::cout << "Generic(" << t.name << ", " << t.type_args.size() << " args)\n";
            for (auto& a : t.type_args) dump_type(*a, indent + 1);
        }
    }, node.data);
    std::cout << "\n";
}

void dump_expr(const Expr& expr, int indent = 0) {
    dump_indent(indent);
    std::cout << "Expr: ";
    std::visit([&](const auto& e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, IntegerLit>) {
            std::cout << "IntegerLit(" << e.value << ")";
        } else if constexpr (std::is_same_v<T, FloatLit>) {
            std::cout << "FloatLit(" << e.value << ")";
        } else if constexpr (std::is_same_v<T, CharLit>) {
            std::cout << "CharLit(" << e.value << ")";
        } else if constexpr (std::is_same_v<T, StringLit>) {
            std::cout << "StringLit(" << e.value << ")";
        } else if constexpr (std::is_same_v<T, BoolLit>) {
            std::cout << "BoolLit(" << e.value << ")";
        } else if constexpr (std::is_same_v<T, NullLit>) {
            std::cout << "NullLit";
        } else if constexpr (std::is_same_v<T, Identifier>) {
            std::cout << "Identifier(" << e.name << ")";
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            static const char* ops[] = {"Add", "Sub", "Mul", "Div", "Mod", "BitAnd", "BitOr", "BitXor", "BitNot", "LShift", "RShift", "Eq", "Neq", "Lt", "Gt", "Le", "Ge", "LogicAnd", "LogicOr"};
            std::cout << "BinaryExpr(" << ops[e.op] << ")\n";
            dump_expr(*e.left, indent + 1);
            dump_expr(*e.right, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            static const char* ops[] = {"Neg", "Not", "BitNot", "Deref", "Addr", "PreInc", "PreDec"};
            std::cout << "UnaryExpr(" << ops[e.op] << ")\n";
            dump_expr(*e.operand, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, PostfixExpr>) {
            static const char* ops[] = {"Inc", "Dec"};
            std::cout << "PostfixExpr(" << ops[e.op] << ")\n";
            dump_expr(*e.operand, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            std::cout << "CallExpr(" << e.args.size() << " args)\n";
            dump_expr(*e.callee, indent + 1);
            for (auto& a : e.args) dump_expr(*a, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
            std::cout << "IndexExpr\n";
            dump_expr(*e.object, indent + 1);
            dump_expr(*e.index, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
            std::cout << "MemberExpr(" << e.member << ")\n";
            dump_expr(*e.object, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, CastExpr>) {
            std::cout << "CastExpr\n";
            dump_type(*e.target_type, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, SuccessExpr>) {
            std::cout << "SuccessExpr\n";
            dump_expr(*e.value, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, FailureExpr>) {
            std::cout << "FailureExpr\n";
            if (e.value) dump_expr(**e.value, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, ResultBranchExpr>) {
            std::cout << "ResultBranchExpr\n";
            dump_expr(*e.expr, indent + 1);
            if (e.on_success) dump_expr(*e.on_success, indent + 1);
            if (e.on_failure) dump_expr(*e.on_failure, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, BuiltinExpr>) {
            std::cout << "BuiltinExpr(" << e.name << ")";
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
            std::cout << "ArrayLiteral(" << e.elements.size() << " elements)\n";
            for (auto& el : e.elements) dump_expr(*el, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, GenericCallExpr>) {
            std::cout << "GenericCallExpr(" << e.type_args.size() << " type args)\n";
            dump_expr(*e.callee, indent + 1);
            return;
        } else if constexpr (std::is_same_v<T, StructLiteral>) {
            std::cout << "StructLiteral(" << e.fields.size() << " fields)\n";
            if (e.type_expr) dump_expr(*e.type_expr, indent + 1);
            for (auto& [fname, fval] : e.fields) {
                dump_indent(indent + 1);
                std::cout << fname << ":\n";
                dump_expr(*fval, indent + 2);
            }
            return;
        }
    }, expr.data);
    std::cout << "\n";
}

void dump_stmt(const Stmt& stmt, int indent = 0) {
    dump_indent(indent);
    std::cout << "Stmt: ";
    std::visit([&](const auto& s) {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, ExprStmt>) {
            std::cout << "ExprStmt\n";
            dump_expr(*s.expr, indent + 1);
        } else if constexpr (std::is_same_v<T, AssignStmt>) {
            std::cout << "AssignStmt\n";
            dump_expr(*s.target, indent + 1);
            dump_expr(*s.value, indent + 1);
        } else if constexpr (std::is_same_v<T, CompoundAssignStmt>) {
            std::cout << "CompoundAssignStmt\n";
            dump_expr(*s.target, indent + 1);
            dump_expr(*s.value, indent + 1);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
            std::cout << "ReturnStmt\n";
            if (s.value) dump_expr(**s.value, indent + 1);
        } else if constexpr (std::is_same_v<T, BreakStmt>) {
            std::cout << "BreakStmt(" << s.levels << ")";
        } else if constexpr (std::is_same_v<T, ContinueStmt>) {
            std::cout << "ContinueStmt(" << s.levels << ")";
        } else if constexpr (std::is_same_v<T, IfStmt>) {
            std::cout << "IfStmt\n";
            dump_expr(*s.condition, indent + 1);
            dump_stmt(*s.then_block, indent + 1);
            if (s.else_block) dump_stmt(**s.else_block, indent + 1);
        } else if constexpr (std::is_same_v<T, WhileStmt>) {
            std::cout << "WhileStmt\n";
            dump_expr(*s.condition, indent + 1);
            dump_stmt(*s.body, indent + 1);
        } else if constexpr (std::is_same_v<T, DoWhileStmt>) {
            std::cout << "DoWhileStmt\n";
            dump_stmt(*s.body, indent + 1);
            dump_expr(*s.condition, indent + 1);
        } else if constexpr (std::is_same_v<T, SwitchStmt>) {
            std::cout << "SwitchStmt\n";
            dump_expr(*s.subject, indent + 1);
            for (auto& c : s.cases) {
                dump_indent(indent + 1);
                std::cout << "Case:\n";
                dump_expr(*c.condition, indent + 2);
                dump_stmt(*c.body, indent + 2);
            }
            if (s.default_case) dump_stmt(**s.default_case, indent + 1);
        } else if constexpr (std::is_same_v<T, MatchStmt>) {
            std::cout << "MatchStmt\n";
            dump_expr(*s.subject, indent + 1);
            for (auto& a : s.arms) {
                dump_indent(indent + 1);
                std::cout << "Arm:\n";
                dump_expr(*a.condition, indent + 2);
                dump_expr(*a.value, indent + 2);
            }
        } else if constexpr (std::is_same_v<T, ForeachStmt>) {
            std::cout << "ForeachStmt(" << s.element_var << ")\n";
            dump_expr(*s.iterable, indent + 1);
            dump_stmt(*s.body, indent + 1);
        } else if constexpr (std::is_same_v<T, Block>) {
            std::cout << "Block(" << s.stmts.size() << " stmts)\n";
            for (auto& st : s.stmts) dump_stmt(*st, indent + 1);
        } else if constexpr (std::is_same_v<T, DeclPtr>) {
            std::cout << "Decl\n";
            dump_decl(*s, indent + 1);
        }
    }, stmt.data);
    std::cout << "\n";
}

void dump_decl(const Decl& decl, int indent = 0) {
    dump_indent(indent);
    std::cout << "Decl: ";
    std::visit([&](const auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, VariableDecl>) {
            std::cout << "VariableDecl(" << d.name << ")\n";
            dump_type(*d.type, indent + 1);
            if (d.init) dump_expr(*d.init, indent + 1);
        } else if constexpr (std::is_same_v<T, ConstantDecl>) {
            std::cout << "ConstantDecl(" << d.name << ")\n";
            if (d.type) dump_type(*d.type, indent + 1);
            dump_expr(*d.init, indent + 1);
        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
            std::cout << "FunctionDecl(" << d.name << ", " << d.params.size() << " params, generics=" << d.generic_params.size() << ")\n";
            for (auto& p : d.params) {
                dump_indent(indent + 1);
                std::cout << "Param: " << p.name << "\n";
                dump_type(*p.type, indent + 2);
            }
            if (d.return_type) dump_type(*d.return_type, indent + 1);
            if (d.body) dump_stmt(*d.body, indent + 1);
        } else if constexpr (std::is_same_v<T, StructDecl>) {
            std::cout << "StructDecl(" << d.name << ")\n";
            for (auto& f : d.fields) {
                dump_indent(indent + 1);
                std::cout << "Field: " << f.name << "\n";
                dump_type(*f.type, indent + 2);
            }
        } else if constexpr (std::is_same_v<T, EnumDecl>) {
            std::cout << "EnumDecl(" << d.name << ")\n";
            for (auto& v : d.variants) {
                dump_indent(indent + 1);
                std::cout << "Variant: " << v.name << "\n";
                if (v.value) dump_expr(**v.value, indent + 2);
            }
        } else if constexpr (std::is_same_v<T, UnionDecl>) {
            std::cout << "UnionDecl(" << d.name << ")\n";
            for (auto& f : d.fields) {
                dump_indent(indent + 1);
                std::cout << "Field: " << f.name << "\n";
                dump_type(*f.type, indent + 2);
            }
        } else if constexpr (std::is_same_v<T, ImportDecl>) {
            std::cout << "ImportDecl(" << d.path << ")";
        } else if constexpr (std::is_same_v<T, ExternBlock>) {
            std::cout << "ExternBlock\n";
            for (auto& decl : d.decls) dump_decl(*decl, indent + 1);
        } else if constexpr (std::is_same_v<T, NamespaceDecl>) {
            std::cout << "NamespaceDecl(" << d.name << ")\n";
            for (auto& decl : d.decls) dump_decl(*decl, indent + 1);
        } else if constexpr (std::is_same_v<T, CompileTimeIf>) {
            std::cout << "CompileTimeIf\n";
            dump_expr(*d.condition, indent + 1);
            for (auto& decl : d.then_decls) dump_decl(*decl, indent + 1);
            for (auto& decl : d.else_decls) dump_decl(*decl, indent + 1);
        }
    }, decl.data);
    std::cout << "\n";
}

void dump_module(const Module& mod) {
    std::cout << "Module(" << mod.decls.size() << " decls)\n";
    for (auto& decl : mod.decls) {
        dump_decl(*decl, 1);
    }
}

} // namespace femto::ast