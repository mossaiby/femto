#pragma once

#include <string>
#include <vector>

#include "ast.h"
#include "common/diagnostic.h"
#include "lexer/token.h"

namespace femto {

class Parser {
public:
    Parser(const std::vector<Token>& tokens, DiagnosticEngine& diag);

    ast::Module parse();

private:
    const std::vector<Token>& tokens_;
    DiagnosticEngine& diag_;
    std::size_t pos_ = 0;

    // Token access
    const Token& peek() const;
    const Token& peek_next() const;
    const Token& advance();
    bool match(TokenType type);
    bool check(TokenType type) const;
    bool is_at_end() const;

    // Error recovery
    void error(const std::string& msg);
    void expect(TokenType type, const std::string& expected);
    void synchronize();

    // Top-level
    ast::DeclPtr parse_decl();
    ast::DeclPtr parse_variable_decl();
    ast::DeclPtr parse_constant_decl();
    ast::DeclPtr parse_function_decl();
    ast::DeclPtr parse_struct_decl();
    ast::DeclPtr parse_enum_decl();
    ast::DeclPtr parse_union_decl();
    ast::DeclPtr parse_namespace_decl();
    ast::DeclPtr parse_extern_block();
    ast::DeclPtr parse_import_decl();
    ast::DeclPtr parse_compile_time_if();

    // Types
    ast::TypePtr parse_type();
    ast::TypePtr parse_basic_type();
    ast::TypePtr parse_function_type();
    ast::TypePtr parse_pointer_type(ast::TypePtr inner);
    ast::TypePtr parse_slice_type(ast::TypePtr inner);
    ast::TypePtr parse_array_type(ast::TypePtr inner);

    // Expressions - precedence climbing
    ast::ExprPtr parse_expr();
    ast::ExprPtr parse_assignment();
    ast::ExprPtr parse_or();
    ast::ExprPtr parse_and();
    ast::ExprPtr parse_bitwise_or();
    ast::ExprPtr parse_bitwise_xor();
    ast::ExprPtr parse_bitwise_and();
    ast::ExprPtr parse_equality();
    ast::ExprPtr parse_comparison();
    ast::ExprPtr parse_shift();
    ast::ExprPtr parse_additive();
    ast::ExprPtr parse_multiplicative();
    ast::ExprPtr parse_unary();
    ast::ExprPtr parse_postfix();
    ast::ExprPtr parse_primary();

    // Helper for postfix
    ast::ExprPtr parse_call_or_index(ast::ExprPtr callee);

    // Statements
    ast::StmtPtr parse_stmt();
    ast::StmtPtr parse_block();
    ast::StmtPtr parse_if_stmt();
    ast::StmtPtr parse_while_stmt();
    ast::StmtPtr parse_do_while_stmt();
    ast::StmtPtr parse_switch_stmt();
    ast::StmtPtr parse_match_stmt();
    ast::StmtPtr parse_foreach_stmt();
    ast::StmtPtr parse_return_stmt();
    ast::StmtPtr parse_break_stmt();
    ast::StmtPtr parse_continue_stmt();

    // Helpers
    std::string parse_name();
    bool is_builtin_type_token() const;
    bool is_type_start() const;
    bool check_type() const;

    // ?? branch arm: (type name) { body }
    ast::ExprPtr parse_branch_arm();
};

} // namespace femto
