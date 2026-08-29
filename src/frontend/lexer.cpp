#include "frontend/lexer.hpp"
#include <cctype>
#include <unordered_map>
#include <string>

namespace femto {

static const std::unordered_map<std::string_view, TokenKind> KEYWORDS = {
    {"import", TokenKind::KwImport}, {"for", TokenKind::KwFor}, {"while", TokenKind::KwWhile},
    {"do", TokenKind::KwDo}, {"if", TokenKind::KwIf}, {"then", TokenKind::KwThen},
    {"else", TokenKind::KwElse}, {"switch", TokenKind::KwSwitch}, {"case", TokenKind::KwCase},
    {"match", TokenKind::KwMatch}, {"return", TokenKind::KwReturn}, {"break", TokenKind::KwBreak},
    {"continue", TokenKind::KwContinue}, {"enum", TokenKind::KwEnum}, {"struct", TokenKind::KwStruct},
    {"null", TokenKind::KwNull}, {"extern", TokenKind::KwExtern}, {"foreach", TokenKind::KwForeach},
    {"array", TokenKind::KwArray}, {"namespace", TokenKind::KwNamespace}, {"success", TokenKind::KwSuccess},
    {"failure", TokenKind::KwFailure}, {"true", TokenKind::KwTrue}, {"false", TokenKind::KwFalse},
    {"void", TokenKind::KwVoid}, {"in", TokenKind::KwIn}, {"default", TokenKind::KwDefault},
    {"const", TokenKind::KwConst}, {"union", TokenKind::KwUnion},
    
    {"int8", TokenKind::KwInt8}, {"int16", TokenKind::KwInt16}, {"int32", TokenKind::KwInt32},
    {"int64", TokenKind::KwInt64}, {"int128", TokenKind::KwInt128}, {"int256", TokenKind::KwInt256}, {"int512", TokenKind::KwInt512},
    {"uint8", TokenKind::KwUint8}, {"uint16", TokenKind::KwUint16}, {"uint32", TokenKind::KwUint32},
    {"uint64", TokenKind::KwUint64}, {"uint128", TokenKind::KwUint128}, {"uint256", TokenKind::KwUint256}, {"uint512", TokenKind::KwUint512},
    {"float16", TokenKind::KwFloat16}, {"float32", TokenKind::KwFloat32}, {"float64", TokenKind::KwFloat64}, {"float128", TokenKind::KwFloat128},
    {"bool8", TokenKind::KwBool8}, {"bool16", TokenKind::KwBool16}, {"bool32", TokenKind::KwBool32},
    {"bool64", TokenKind::KwBool64}, {"bool128", TokenKind::KwBool128}, {"bool256", TokenKind::KwBool256}, {"bool512", TokenKind::KwBool512},
    {"char8", TokenKind::KwChar8}, {"char16", TokenKind::KwChar16}, {"char32", TokenKind::KwChar32},
    {"string8", TokenKind::KwString8}, {"string16", TokenKind::KwString16}, {"string32", TokenKind::KwString32}
};

void Lexer::skip_whitespace_and_comments() {
    while (cursor_ < src_.size()) {
        char c = peek();
        if (std::isspace(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (cursor_ < src_.size() && peek() != '\n') advance();
        } else if (c == '/' && peek(1) == '*') {
            advance(); advance();
            uint32_t depth = 1;
            while (cursor_ < src_.size() && depth > 0) {
                if (peek() == '/' && peek(1) == '*') {
                    advance(); advance();
                    depth++;
                } else if (peek() == '*' && peek(1) == '/') {
                    advance(); advance();
                    depth--;
                } else {
                    advance();
                }
            }
        } else {
            break;
        }
    }
}

Token Lexer::next_token() {
    skip_whitespace_and_comments();
    if (cursor_ >= src_.size()) {
        return Token{TokenKind::Eof, SourceSpan{SourceLocation{cursor_}, 0}, ""};
    }

    uint32_t start_pos = cursor_;
    char c = advance();

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        cursor_ = start_pos;
        return scan_identifier_or_keyword();
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
        cursor_ = start_pos;
        return scan_number();
    }
    if (c == '"') {
        cursor_ = start_pos;
        return scan_string();
    }
    if (c == '`') {
        cursor_ = start_pos;
        return scan_raw_string();
    }
    if (c == '\'') {
        cursor_ = start_pos;
        return scan_char();
    }

    auto make_tok = [&](TokenKind kind, uint32_t len) {
        return Token{kind, SourceSpan{SourceLocation{start_pos}, len}, 
                     src_.substr(start_pos, len)};
    };

    switch (c) {
        case ':':
            if (peek() == ':') { advance(); return make_tok(TokenKind::DoubleColon, 2); }
            return make_tok(TokenKind::Colon, 1);
        case ';': return make_tok(TokenKind::Semicolon, 1);
        case ',': return make_tok(TokenKind::Comma, 1);
        case '.': 
            if (peek() == '.' && peek(1) == '.') { advance(); advance(); return make_tok(TokenKind::DotDotDot, 3); }
            if (peek() == '.') { advance(); return make_tok(TokenKind::DotDot, 2); }
            return make_tok(TokenKind::Dot, 1);
        case '?':
            if (peek() == '?') { advance(); return make_tok(TokenKind::QuestionQuestion, 2); }
            break;
        case '!':
            if (peek() == '=') { advance(); return make_tok(TokenKind::BangEq, 2); }
            return make_tok(TokenKind::Bang, 1);
        case '=':
            if (peek() == '=') { advance(); return make_tok(TokenKind::EqEq, 2); }
            return make_tok(TokenKind::Eq, 1);
        case '+':
            if (peek() == '+') { advance(); return make_tok(TokenKind::PlusPlus, 2); }
            if (peek() == '=') { advance(); return make_tok(TokenKind::PlusEq, 2); }
            return make_tok(TokenKind::Plus, 1);
        case '-':
            if (peek() == '>') { advance(); return make_tok(TokenKind::Arrow, 2); }
            if (peek() == '-') { advance(); return make_tok(TokenKind::MinusMinus, 2); }
            if (peek() == '=') { advance(); return make_tok(TokenKind::MinusEq, 2); }
            return make_tok(TokenKind::Minus, 1);
        case '*':
            if (peek() == '=') { advance(); return make_tok(TokenKind::StarEq, 2); }
            return make_tok(TokenKind::Star, 1);
        case '/':
            if (peek() == '=') { advance(); return make_tok(TokenKind::SlashEq, 2); }
            return make_tok(TokenKind::Slash, 1);
        case '%':
            if (peek() == '=') { advance(); return make_tok(TokenKind::PercentEq, 2); }
            return make_tok(TokenKind::Percent, 1);
        case '&':
            if (peek() == '&') { advance(); return make_tok(TokenKind::AmpAmp, 2); }
            return make_tok(TokenKind::Amp, 1);
        case '|':
            if (peek() == '|') { advance(); return make_tok(TokenKind::PipePipe, 2); }
            return make_tok(TokenKind::Pipe, 1);
        case '^': return make_tok(TokenKind::Caret, 1);
        case '~': return make_tok(TokenKind::Tilde, 1);
        case '<':
            if (peek() == '=') { advance(); return make_tok(TokenKind::LtEq, 2); }
            if (peek() == '<') { advance(); return make_tok(TokenKind::Shl, 2); }
            return make_tok(TokenKind::Lt, 1);
        case '>':
            if (peek() == '=') { advance(); return make_tok(TokenKind::GtEq, 2); }
            if (peek() == '>') { advance(); return make_tok(TokenKind::Shr, 2); }
            return make_tok(TokenKind::Gt, 1);
        case '(': return make_tok(TokenKind::LParen, 1);
        case ')': return make_tok(TokenKind::RParen, 1);
        case '[': return make_tok(TokenKind::LBracket, 1);
        case ']': return make_tok(TokenKind::RBracket, 1);
        case '{': return make_tok(TokenKind::LBrace, 1);
        case '}': return make_tok(TokenKind::RBrace, 1);
        case '#':
            if (peek() == 'e' && src_.substr(cursor_, 6) == "export") {
                cursor_ += 6;
                return make_tok(TokenKind::HashExport, 7);
            }
            if (peek() == 'i' && src_.substr(cursor_, 2) == "if") {
                cursor_ += 2;
                return make_tok(TokenKind::HashIf, 3);
            }
            if (peek() == 'e' && src_.substr(cursor_, 4) == "else") {
                cursor_ += 4;
                return make_tok(TokenKind::HashElse, 5);
            }
            return make_tok(TokenKind::HashSubject, 1);
        case '@':
            while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
            return make_tok(TokenKind::AtBuiltin, cursor_ - start_pos);
    }

    std::string err = "unexpected character '";
    err += c;
    err += "'";
    diag_.report_error(SourceSpan{SourceLocation{start_pos}, 1}, err);
    return next_token();
}

Token Lexer::scan_identifier_or_keyword() {
    uint32_t start_pos = cursor_;
    while (cursor_ < src_.size() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
        advance();
    }
    std::string_view text = src_.substr(start_pos, cursor_ - start_pos);
    TokenKind kind = TokenKind::Identifier;
    if (auto it = KEYWORDS.find(text); it != KEYWORDS.end()) {
        kind = it->second;
    }
    return Token{kind, SourceSpan{SourceLocation{start_pos}, static_cast<uint32_t>(text.size())}, text};
}

Token Lexer::scan_number() {
    uint32_t start_pos = cursor_;
    bool is_float = false;
    
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'b' || peek(1) == 'o')) {
        advance(); advance();
        while (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    } else {
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            is_float = true;
            advance();
            while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') advance();
            while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_') advance();
        }
    }
    std::string_view text = src_.substr(start_pos, cursor_ - start_pos);
    return Token{is_float ? TokenKind::FloatLiteral : TokenKind::IntLiteral,
                 SourceSpan{SourceLocation{start_pos}, static_cast<uint32_t>(text.size())}, text};
}

Token Lexer::scan_string() {
    uint32_t start_pos = cursor_;
    advance();
    while (cursor_ < src_.size() && peek() != '"') {
        if (peek() == '\\') advance();
        advance();
    }
    if (cursor_ < src_.size()) advance();
    std::string_view text = src_.substr(start_pos, cursor_ - start_pos);
    return Token{TokenKind::StringLiteral, SourceSpan{SourceLocation{start_pos}, static_cast<uint32_t>(text.size())}, text};
}

Token Lexer::scan_raw_string() {
    uint32_t start_pos = cursor_;
    advance();
    while (cursor_ < src_.size() && peek() != '`') advance();
    if (cursor_ < src_.size()) advance();
    std::string_view text = src_.substr(start_pos, cursor_ - start_pos);
    return Token{TokenKind::RawStringLiteral, SourceSpan{SourceLocation{start_pos}, static_cast<uint32_t>(text.size())}, text};
}

Token Lexer::scan_char() {
    uint32_t start_pos = cursor_;
    advance();
    if (peek() == '\\') {
        advance();
        if (peek() == 'u' && peek(1) == '{') {
            advance(); advance();
            while (peek() != '}' && cursor_ < src_.size()) advance();
            advance();
        } else {
            advance();
        }
    } else {
        advance();
    }
    advance();
    std::string_view text = src_.substr(start_pos, cursor_ - start_pos);
    return Token{TokenKind::CharLiteral, SourceSpan{SourceLocation{start_pos}, static_cast<uint32_t>(text.size())}, text};
}

} // namespace femto