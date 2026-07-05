#pragma once

#include <string>
#include <vector>

#include "common/diagnostic.h"
#include "token.h"

namespace femto {

class Lexer {
public:
    Lexer(const std::string& source, DiagnosticEngine& diag);

    std::vector<Token> tokenize();

private:
    const std::string& source_;
    DiagnosticEngine& diag_;
    std::size_t pos_ = 0;
    SourceLocation current_loc_{1, 1, 0};

    // Core scanning
    char peek() const;
    char peek_next() const;
    char advance();
    bool match(char expected);
    bool match2(char a, char b);

    // Token building
    Token make_token(TokenType type, std::string lexeme = "",
                     std::variant<std::monostate, std::int64_t, double, std::string> value = {});
    Token make_token_at(TokenType type, SourceLocation start, std::string lexeme = "",
                        std::variant<std::monostate, std::int64_t, double, std::string> value = {});

    // Scanning functions
    Token scan_token();
    Token scan_number();
    Token scan_string();
    Token scan_raw_string();
    Token scan_char();
    Token scan_identifier_or_keyword();
    Token scan_line_comment();
    Token scan_block_comment();

    // Helpers
    void skip_whitespace();
    void skip_newline();
    bool is_at_end() const;
    bool is_digit(char c) const;
    bool is_hex_digit(char c) const;
    bool is_alpha(char c) const;
    bool is_alnum(char c) const;

    // Escape sequence processing
    bool process_escape_sequence(char& result);
    std::int64_t parse_integer_literal(const std::string& text);
    double parse_float_literal(const std::string& text);
};

} // namespace femto
