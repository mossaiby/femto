#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "parser/ast.h"
#include "symbol_table.h"
#include "type_system.h"
#include "common/diagnostic.h"

namespace femto::sema {

struct FunctionSignature {
    std::string name;
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_error_return = false;
    bool is_extern = false;
    const ast::FunctionDecl* decl = nullptr;
};

class TypeChecker {
public:
    TypeChecker(DiagnosticEngine& diag);

    bool check(ast::Module& mod);

    const SymbolTable& symbols() const { return symbols_; }

private:
    DiagnosticEngine& diag_;
    SymbolTable symbols_;

    // Built-in type mapping
    TypePtr resolve_builtin_type(TokenType tt);
    TypePtr resolve_type(const ast::TypeNode& node);

    // Declaration checking
    void check_decl(ast::Decl& decl);
    void check_variable_decl(ast::VariableDecl& var);
    void check_constant_decl(ast::ConstantDecl& con);
    void check_function_decl(ast::FunctionDecl& func);
    void check_struct_decl(ast::StructDecl& str);
    void check_enum_decl(ast::EnumDecl& en);
    void check_union_decl(ast::UnionDecl& un);
    void check_namespace_decl(ast::NamespaceDecl& ns);
    void check_extern_block(ast::ExternBlock& block);
    void check_import_decl(ast::ImportDecl& imp);

    // Statement checking
    TypePtr check_stmt(ast::Stmt& stmt);
    TypePtr check_block(ast::Block& block);
    TypePtr check_if_stmt(ast::IfStmt& stmt);
    TypePtr check_while_stmt(ast::WhileStmt& stmt);
    TypePtr check_do_while_stmt(ast::DoWhileStmt& stmt);
    TypePtr check_switch_stmt(ast::SwitchStmt& stmt);
    TypePtr check_return_stmt(ast::ReturnStmt& stmt);

    // Expression checking - returns the type of the expression
    TypePtr check_expr(ast::Expr& expr);
    TypePtr check_binary_expr(ast::BinaryExpr& expr);
    TypePtr check_unary_expr(ast::UnaryExpr& expr);
    TypePtr check_call_expr(ast::CallExpr& expr);
    TypePtr check_assign_expr(ast::AssignStmt& stmt);

    // Overload resolution
    TypePtr resolve_call(const std::string& name, const std::vector<TypePtr>& arg_types, ast::CallExpr& expr);
    bool match_overload(const FunctionSignature& sig, const std::vector<TypePtr>& arg_types);

    // Struct literal checking
    TypePtr check_struct_literal(ast::StructLiteral& lit, const std::string& expected_name = "",
                                 const std::vector<TypePtr>& generic_args = {});

    // Struct/enum/function declaration storage for field/member access
    std::unordered_map<std::string, ast::StructDecl*> struct_decls_;
    std::unordered_map<std::string, ast::EnumDecl*> enum_decls_;
    std::unordered_map<std::string, ast::FunctionDecl*> func_decls_;

    // Substitute generic type parameters in a type
    TypePtr substitute_type(TypePtr type, const std::unordered_map<std::string, TypePtr>& generic_map);

    // Current function context
    const FunctionSignature* current_func_ = nullptr;
    TypePtr current_return_type_;
};

} // namespace femto::sema
