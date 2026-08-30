#pragma once
#include "frontend/lexer.hpp"
#include "frontend/ast.hpp"
#include "common/arena.hpp"

namespace femto {

class Parser {
public:
    Parser(Lexer& lexer, Arena& arena, Diagnostics& diag)
        : lexer_(lexer), arena_(arena), diag_(diag) {
        current_ = lexer_.next_token();
        peek_token_ = lexer_.next_token();
    }

    ASTProgram parse_program();

private:
    Token current_;
    Token peek_token_;
    Lexer& lexer_;
    Arena& arena_;
    Diagnostics& diag_;

    void advance() {
        current_ = peek_token_;
        peek_token_ = lexer_.next_token();
    }

    bool match(TokenKind kind) {
        if (current_.kind == kind) {
            advance();
            return true;
        }
        return false;
    }

    bool match_gt();

    Token expect(TokenKind kind, std::string_view msg) {
        if (current_.kind == kind) {
            Token t = current_;
            advance();
            return t;
        }
        diag_.report_error(current_.span, msg);
        return current_;
    }

    // Top-Level Declarations
    void parse_top_level_declaration(ASTProgram& prog, bool is_exported = false);
    ASTFunctionDecl* parse_function_decl(std::string_view name, std::vector<std::string_view> generic_params, bool is_exported, bool is_extern_c = false);
    ASTStructDecl* parse_struct_decl(std::string_view name, bool is_exported);
    ASTUnionDecl* parse_union_decl(std::string_view name, bool is_exported);
    ASTEnumDecl* parse_enum_decl(std::string_view name, bool is_exported);
    ASTConstDecl* parse_const_decl(std::string_view name, bool is_exported);

    ASTType* parse_type();

    // Statements
    ASTStmt* parse_statement();
    ASTStmt* parse_var_decl_stmt(ASTType* type_annot);
    ASTStmt* parse_if_stmt();
    ASTStmt* parse_while_stmt();
    ASTStmt* parse_do_while_stmt();
    ASTStmt* parse_for_stmt();
    ASTStmt* parse_switch_stmt();
    ASTStmt* parse_foreach_stmt();
    ASTStmt* parse_break_stmt();
    ASTStmt* parse_continue_stmt();
    ASTStmt* parse_hash_if_stmt();
    ASTStmt* parse_defer_stmt();
    std::vector<ASTStmt*> parse_block();

    // Expressions (Pratt Parser)
    ASTExpr* parse_expression(int min_precedence = 0);
    ASTExpr* parse_primary_expression();
    int get_binary_precedence(TokenKind kind);

    bool is_generic_type_start();
    bool is_generic_call_at(uint32_t lt_offset);
};

} // namespace femto