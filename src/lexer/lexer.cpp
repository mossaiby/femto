#include "lexer.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <sstream>

namespace femto {

Lexer::Lexer(const std::string& source, DiagnosticEngine& diag)
    : source_(source), diag_(diag) {}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (!is_at_end()) {
        skip_whitespace();
        if (is_at_end()) break;

        // Track start location for the token
        SourceLocation start = current_loc_;
        Token tok = scan_token();
        tok.span.start = start;
        tok.span.end = current_loc_;
        tokens.push_back(std::move(tok));
    }

    Token eof;
    eof.type = TokenType::Eof;
    eof.span.start = current_loc_;
    eof.span.end = current_loc_;
    tokens.push_back(std::move(eof));

    return tokens;
}

char Lexer::peek() const {
    if (is_at_end()) return '\0';
    return source_[pos_];
}

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) return '\0';
    return source_[pos_ + 1];
}

char Lexer::advance() {
    char c = source_[pos_];
    pos_++;
    if (c == '\n') {
        current_loc_.line++;
        current_loc_.column = 1;
    } else {
        current_loc_.column++;
    }
    current_loc_.offset = static_cast<std::uint32_t>(pos_);
    return c;
}

bool Lexer::match(char expected) {
    if (is_at_end() || source_[pos_] != expected) return false;
    advance();
    return true;
}

bool Lexer::match2(char a, char b) {
    if (pos_ + 1 >= source_.size()) return false;
    if (source_[pos_] != a || source_[pos_ + 1] != b) return false;
    advance();
    advance();
    return true;
}

Token Lexer::make_token(TokenType type, std::string lexeme,
                        std::variant<std::monostate, std::int64_t, double, std::string> value) {
    Token tok;
    tok.type = type;
    tok.lexeme = std::move(lexeme);
    tok.value = std::move(value);
    return tok;
}

Token Lexer::make_token_at(TokenType type, SourceLocation start, std::string lexeme,
                           std::variant<std::monostate, std::int64_t, double, std::string> value) {
    Token tok;
    tok.type = type;
    tok.span.start = start;
    tok.span.end = current_loc_;
    tok.lexeme = std::move(lexeme);
    tok.value = std::move(value);
    return tok;
}

Token Lexer::scan_token() {
    char c = peek();

    // Single character tokens
    switch (c) {
        case '(':  advance(); return make_token(TokenType::LParen, "(");
        case ')':  advance(); return make_token(TokenType::RParen, ")");
        case '{':  advance(); return make_token(TokenType::LBrace, "{");
        case '}':  advance(); return make_token(TokenType::RBrace, "}");
        case '[':  advance(); return make_token(TokenType::LBracket, "[");
        case ']':  advance(); return make_token(TokenType::RBracket, "]");
        case ',':  advance(); return make_token(TokenType::Comma, ",");
        case ';':  advance(); return make_token(TokenType::Semicolon, ";");
        case '.':  advance(); return make_token(TokenType::Dot, ".");
        case '~':  advance(); return make_token(TokenType::Tilde, "~");
        case '\n': advance(); return make_token(TokenType::Newline, "\\n");
        case '@':
            advance();
            if (is_alpha(peek()) || peek() == '_') {
                // @-builtin like @target, @arch, etc.
                std::string text = "@";
                while (!is_at_end() && (is_alnum(peek()) || peek() == '_')) {
                    text += advance();
                }
                std::string val = text;
                return make_token(TokenType::Identifier, std::move(text), std::move(val));
            }
            return make_token(TokenType::At, "@");
        case '#':
            advance();
            if (is_alpha(peek())) {
                // #export, #if, #else
                std::string text = "#";
                while (!is_at_end() && (is_alnum(peek()) || peek() == '_')) {
                    text += advance();
                }
                if (text == "#export") return make_token(TokenType::HashExport, text);
                if (text == "#if") return make_token(TokenType::HashIf, text);
                if (text == "#else") return make_token(TokenType::HashElse, text);
                std::string val = text;
                return make_token(TokenType::Identifier, std::move(text), std::move(val));
            }
            return make_token(TokenType::Hash, "#");
        case ':':
            advance();
            if (match(':')) return make_token(TokenType::DoubleColon, "::");
            return make_token(TokenType::Colon, ":");
        case '+':
            advance();
            if (match('+')) return make_token(TokenType::PlusPlus, "++");
            if (match('=')) return make_token(TokenType::PlusEq, "+=");
            return make_token(TokenType::Plus, "+");
        case '-':
            advance();
            if (match('-')) return make_token(TokenType::MinusMinus, "--");
            if (match('=')) return make_token(TokenType::MinusEq, "-=");
            if (match('>')) return make_token(TokenType::Arrow, "->");
            return make_token(TokenType::Minus, "-");
        case '*':
            advance();
            if (match('=')) return make_token(TokenType::StarEq, "*=");
            return make_token(TokenType::Star, "*");
        case '/':
            advance();
            if (match('/')) return scan_line_comment();
            if (match('*')) return scan_block_comment();
            if (match('=')) return make_token(TokenType::SlashEq, "/=");
            return make_token(TokenType::Slash, "/");
        case '%':
            advance();
            if (match('=')) return make_token(TokenType::PercentEq, "%=");
            return make_token(TokenType::Percent, "%");
        case '&':
            advance();
            if (match('&')) return make_token(TokenType::AmpAmp, "&&");
            if (match('=')) return make_token(TokenType::AmpEq, "&=");
            return make_token(TokenType::Amp, "&");
        case '|':
            advance();
            if (match('|')) return make_token(TokenType::PipePipe, "||");
            if (match('=')) return make_token(TokenType::PipeEq, "|=");
            return make_token(TokenType::Pipe, "|");
        case '^':
            advance();
            if (match('=')) return make_token(TokenType::CaretEq, "^=");
            return make_token(TokenType::Caret, "^");
        case '<':
            advance();
            if (match('<')) {
                if (match('=')) return make_token(TokenType::LShiftEq, "<<=");
                return make_token(TokenType::LShift, "<<");
            }
            if (match('=')) return make_token(TokenType::Le, "<=");
            return make_token(TokenType::Lt, "<");
        case '>':
            advance();
            if (match('>')) {
                if (match('=')) return make_token(TokenType::RShiftEq, ">>=");
                return make_token(TokenType::RShift, ">>");
            }
            if (match('=')) return make_token(TokenType::Ge, ">=");
            return make_token(TokenType::Gt, ">");
        case '=':
            advance();
            if (match('=')) return make_token(TokenType::EqEq, "==");
            return make_token(TokenType::Eq, "=");
        case '!':
            advance();
            if (match('=')) return make_token(TokenType::Neq, "!=");
            return make_token(TokenType::Bang, "!");
        case '?':
            advance();
            if (match('?')) return make_token(TokenType::DoubleQuestion, "??");
            return make_token(TokenType::Question, "?");
        case '\'':
            advance();
            return scan_char();
        case '"':
            advance();
            return scan_string();
        case '`':
            advance();
            return scan_raw_string();
    }

    if (is_digit(c)) return scan_number();
    if (is_alpha(c) || c == '_') return scan_identifier_or_keyword();

    advance();
    diag_.error(current_loc_, std::string("unexpected character: '") + c + "'");
    return make_token(TokenType::Error, std::string(1, c));
}

Token Lexer::scan_number() {
    SourceLocation start = current_loc_;
    std::string text;
    bool is_float = false;

    // First digit already peeked, consume it
    text += advance();

    // Check for 0x, 0b, 0o prefixes
    if (text == "0" && (peek() == 'x' || peek() == 'X')) {
        text += advance(); // 'x'
        while (!is_at_end() && (is_hex_digit(peek()) || peek() == '_')) {
            text += advance();
        }
        std::int64_t val = parse_integer_literal(text);
        return make_token_at(TokenType::IntegerLiteral, start, text, val);
    }

    if (text == "0" && (peek() == 'b' || peek() == 'B')) {
        text += advance(); // 'b'
        while (!is_at_end() && (peek() == '0' || peek() == '1' || peek() == '_')) {
            text += advance();
        }
        std::int64_t val = parse_integer_literal(text);
        return make_token_at(TokenType::IntegerLiteral, start, text, val);
    }

    if (text == "0" && (peek() == 'o' || peek() == 'O')) {
        text += advance(); // 'o'
        while (!is_at_end() && ((peek() >= '0' && peek() <= '7') || peek() == '_')) {
            text += advance();
        }
        std::int64_t val = parse_integer_literal(text);
        return make_token_at(TokenType::IntegerLiteral, start, text, val);
    }

    // Remaining decimal digits
    while (!is_at_end() && (is_digit(peek()) || peek() == '_')) {
        text += advance();
    }

    // Check for float
    if (!is_at_end() && peek() == '.' && is_digit(peek_next())) {
        is_float = true;
        text += advance(); // '.'
        while (!is_at_end() && (is_digit(peek()) || peek() == '_')) {
            text += advance();
        }
    }

    // Exponent
    if (!is_at_end() && (peek() == 'e' || peek() == 'E')) {
        is_float = true;
        text += advance(); // 'e' or 'E'
        if (!is_at_end() && (peek() == '+' || peek() == '-')) {
            text += advance();
        }
        while (!is_at_end() && (is_digit(peek()) || peek() == '_')) {
            text += advance();
        }
    }

    if (is_float) {
        double val = parse_float_literal(text);
        return make_token_at(TokenType::FloatLiteral, start, text, val);
    }

    std::int64_t val = parse_integer_literal(text);
    return make_token_at(TokenType::IntegerLiteral, start, text, val);
}

Token Lexer::scan_string() {
    SourceLocation start = current_loc_;
    std::string value;

    while (!is_at_end() && peek() != '"') {
        if (peek() == '\\') {
            advance(); // skip backslash
            char escaped;
            if (!process_escape_sequence(escaped)) {
                diag_.error(current_loc_, "invalid escape sequence");
                return make_token_at(TokenType::Error, start);
            }
            value += escaped;
        } else if (peek() == '\n') {
            diag_.error(current_loc_, "unterminated string literal");
            return make_token_at(TokenType::Error, start);
        } else {
            value += advance();
        }
    }

    if (is_at_end()) {
        diag_.error(start, "unterminated string literal");
        return make_token_at(TokenType::Error, start);
    }

    advance(); // closing "
    return make_token_at(TokenType::StringLiteral, start, "\"" + value + "\"", std::move(value));
}

Token Lexer::scan_raw_string() {
    SourceLocation start = current_loc_;
    std::string value;

    while (!is_at_end() && peek() != '`') {
        if (peek() == '\n') {
            diag_.error(current_loc_, "unterminated raw string literal");
            return make_token_at(TokenType::Error, start);
        }
        value += advance();
    }

    if (is_at_end()) {
        diag_.error(start, "unterminated raw string literal");
        return make_token_at(TokenType::Error, start);
    }

    advance(); // closing `
    return make_token_at(TokenType::StringLiteral, start, "`" + value + "`", std::move(value));
}

Token Lexer::scan_char() {
    SourceLocation start = current_loc_;
    std::string value;

    if (is_at_end() || peek() == '\n') {
        diag_.error(start, "unterminated character literal");
        return make_token_at(TokenType::Error, start);
    }

    if (peek() == '\\') {
        advance();
        char escaped;
        if (!process_escape_sequence(escaped)) {
            diag_.error(current_loc_, "invalid escape sequence");
            return make_token_at(TokenType::Error, start);
        }
        value += escaped;
    } else {
        value += advance();
    }

    if (is_at_end() || peek() != '\'') {
        diag_.error(start, "unterminated character literal");
        return make_token_at(TokenType::Error, start);
    }

    advance(); // closing '
    return make_token_at(TokenType::CharLiteral, start, "'" + value + "'", std::move(value));
}

Token Lexer::scan_identifier_or_keyword() {
    SourceLocation start = current_loc_;
    std::string text;

    while (!is_at_end() && (is_alnum(peek()) || peek() == '_')) {
        text += advance();
    }

    // Check keywords
    static const std::unordered_map<std::string, TokenType> keywords = {
        {"import", TokenType::KW_import}, {"for", TokenType::KW_for},
        {"while", TokenType::KW_while}, {"do", TokenType::KW_do},
        {"if", TokenType::KW_if}, {"then", TokenType::KW_then},
        {"else", TokenType::KW_else}, {"switch", TokenType::KW_switch},
        {"case", TokenType::KW_case}, {"match", TokenType::KW_match},
        {"return", TokenType::KW_return}, {"break", TokenType::KW_break},
        {"continue", TokenType::KW_continue}, {"enum", TokenType::KW_enum},
        {"struct", TokenType::KW_struct}, {"null", TokenType::KW_null},
        {"extern", TokenType::KW_extern}, {"foreach", TokenType::KW_foreach},
        {"array", TokenType::KW_array}, {"namespace", TokenType::KW_namespace},
        {"success", TokenType::KW_success}, {"failure", TokenType::KW_failure},
        {"true", TokenType::KW_true}, {"false", TokenType::KW_false},
        {"void", TokenType::KW_void}, {"in", TokenType::KW_in},
        {"default", TokenType::KW_default}, {"const", TokenType::KW_const},
        {"union", TokenType::KW_union},
        {"class", TokenType::KW_class}, {"interface", TokenType::KW_interface},
        {"defer", TokenType::KW_defer},
    };

    auto it = keywords.find(text);
    if (it != keywords.end()) {
        if (it->second == TokenType::KW_true || it->second == TokenType::KW_false) {
            return make_token_at(TokenType::BoolLiteral, start, text, text == "true");
        }
        return make_token_at(it->second, start, text);
    }

    // Check builtin types
    static const std::unordered_map<std::string, TokenType> types = {
        {"int8", TokenType::TY_int8}, {"int16", TokenType::TY_int16},
        {"int32", TokenType::TY_int32}, {"int64", TokenType::TY_int64},
        {"int128", TokenType::TY_int128}, {"int256", TokenType::TY_int256},
        {"int512", TokenType::TY_int512},
        {"uint8", TokenType::TY_uint8}, {"uint16", TokenType::TY_uint16},
        {"uint32", TokenType::TY_uint32}, {"uint64", TokenType::TY_uint64},
        {"uint128", TokenType::TY_uint128}, {"uint256", TokenType::TY_uint256},
        {"uint512", TokenType::TY_uint512},
        {"float16", TokenType::TY_float16}, {"float32", TokenType::TY_float32},
        {"float64", TokenType::TY_float64}, {"float128", TokenType::TY_float128},
        {"bool8", TokenType::TY_bool8}, {"bool16", TokenType::TY_bool16},
        {"bool32", TokenType::TY_bool32}, {"bool64", TokenType::TY_bool64},
        {"bool128", TokenType::TY_bool128}, {"bool256", TokenType::TY_bool256},
        {"bool512", TokenType::TY_bool512},
        {"char8", TokenType::TY_char8}, {"char16", TokenType::TY_char16},
        {"char32", TokenType::TY_char32},
        {"string8", TokenType::TY_string8}, {"string16", TokenType::TY_string16},
        {"string32", TokenType::TY_string32},
    };

    auto ti = types.find(text);
    if (ti != types.end()) {
        return make_token_at(ti->second, start, text);
    }

    std::string val = text;
    return make_token_at(TokenType::Identifier, start, std::move(text), std::move(val));
}

Token Lexer::scan_line_comment() {
    SourceLocation start = current_loc_;
    while (!is_at_end() && peek() != '\n') {
        advance();
    }
    // Skip whitespace and return next real token
    skip_whitespace();
    if (is_at_end()) {
        return make_token_at(TokenType::Eof, start);
    }
    return scan_token();
}

Token Lexer::scan_block_comment() {
    SourceLocation start = current_loc_;
    int depth = 1;

    while (!is_at_end() && depth > 0) {
        if (peek() == '/' && peek_next() == '*') {
            advance();
            advance();
            depth++;
        } else if (peek() == '*' && peek_next() == '/') {
            advance();
            advance();
            depth--;
        } else {
            advance();
        }
    }

    if (depth > 0) {
        diag_.error(start, "unterminated block comment");
        return make_token_at(TokenType::Error, start);
    }

    // Skip and return next real token
    skip_whitespace();
    if (is_at_end()) {
        return make_token_at(TokenType::Eof, start);
    }
    return scan_token();
}

void Lexer::skip_whitespace() {
    while (!is_at_end()) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
        } else {
            break;
        }
    }
}

bool Lexer::is_at_end() const {
    return pos_ >= source_.size();
}

bool Lexer::is_digit(char c) const {
    return c >= '0' && c <= '9';
}

bool Lexer::is_hex_digit(char c) const {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool Lexer::is_alpha(char c) const {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool Lexer::is_alnum(char c) const {
    return is_alpha(c) || is_digit(c);
}

bool Lexer::process_escape_sequence(char& result) {
    if (is_at_end()) return false;
    char c = advance();
    switch (c) {
        case 'n':  result = '\n'; return true;
        case 't':  result = '\t'; return true;
        case 'r':  result = '\r'; return true;
        case '\\': result = '\\'; return true;
        case '\'': result = '\''; return true;
        case '"':  result = '"';  return true;
        case '0':  result = '\0'; return true;
        case 'u': {
            // Unicode escape: \u{XXXXX}
            if (peek() == '{') {
                advance(); // {
                std::string hex;
                while (!is_at_end() && peek() != '}') {
                    hex += advance();
                }
                if (!is_at_end()) advance(); // }
                if (!hex.empty()) {
                    unsigned long codepoint = std::stoul(hex, nullptr, 16);
                    if (codepoint <= 0x7F) {
                        result = static_cast<char>(codepoint);
                    } else {
                        result = static_cast<char>(0xE0 | (codepoint >> 12));
                    }
                    return true;
                }
            }
            return false;
        }
        default:
            diag_.error(current_loc_, std::string("unknown escape sequence: \\") + c);
            return false;
    }
}

std::int64_t Lexer::parse_integer_literal(const std::string& text) {
    std::string clean;
    for (char c : text) {
        if (c != '_') clean += c;
    }

    if (clean.starts_with("0x") || clean.starts_with("0X")) {
        std::int64_t val = 0;
        auto [ptr, ec] = std::from_chars(clean.data() + 2, clean.data() + clean.size(), val, 16);
        return val;
    }
    if (clean.starts_with("0b") || clean.starts_with("0B")) {
        std::int64_t val = 0;
        for (std::size_t i = 2; i < clean.size(); ++i) {
            val = (val << 1) | (clean[i] - '0');
        }
        return val;
    }
    if (clean.starts_with("0o") || clean.starts_with("0O")) {
        std::int64_t val = 0;
        auto [ptr, ec] = std::from_chars(clean.data() + 2, clean.data() + clean.size(), val, 8);
        return val;
    }

    std::int64_t val = 0;
    auto [ptr, ec] = std::from_chars(clean.data(), clean.data() + clean.size(), val, 10);
    return val;
}

double Lexer::parse_float_literal(const std::string& text) {
    std::string clean;
    for (char c : text) {
        if (c != '_') clean += c;
    }
    char* end;
    double val = std::strtod(clean.c_str(), &end);
    return val;
}

} // namespace femto
