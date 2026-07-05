#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "common/source_location.h"

namespace femto {

enum class TokenType {
    // Literals
    IntegerLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    BoolLiteral,

    // Identifiers
    Identifier,

    // Keywords
    KW_import, KW_for, KW_while, KW_do, KW_if, KW_then, KW_else,
    KW_switch, KW_case, KW_match, KW_return, KW_break, KW_continue,
    KW_enum, KW_struct, KW_null, KW_extern, KW_foreach, KW_array,
    KW_namespace, KW_success, KW_failure, KW_true, KW_false, KW_void,
    KW_in, KW_default, KW_const, KW_union,

    // Reserved keywords
    KW_class, KW_interface, KW_defer,

    // Builtin types
    TY_int8, TY_int16, TY_int32, TY_int64, TY_int128, TY_int256, TY_int512,
    TY_uint8, TY_uint16, TY_uint32, TY_uint64, TY_uint128, TY_uint256, TY_uint512,
    TY_float16, TY_float32, TY_float64, TY_float128,
    TY_bool8, TY_bool16, TY_bool32, TY_bool64, TY_bool128, TY_bool256, TY_bool512,
    TY_char8, TY_char16, TY_char32,
    TY_string8, TY_string16, TY_string32,

    // Operators
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent,        // %
    Amp,            // &
    Pipe,           // |
    Caret,          // ^
    Tilde,          // ~
    Bang,           // !
    Question,       // ?
    DoubleQuestion, // ??
    AmpAmp,         // &&
    PipePipe,       // ||
    EqEq,           // ==
    Neq,            // !=
    Lt,             // <
    Gt,             // >
    Le,             // <=
    Ge,             // >=
    LShift,         // <<
    RShift,         // >>
    Eq,             // =
    PlusEq,         // +=
    MinusEq,        // -=
    StarEq,         // *=
    SlashEq,        // /=
    PercentEq,      // %=
    AmpEq,          // &=
    PipeEq,         // |=
    CaretEq,        // ^=
    LShiftEq,       // <<=
    RShiftEq,       // >>=
    PlusPlus,       // ++
    MinusMinus,     // --
    Arrow,          // ->
    DoubleColon,    // ::
    Colon,          // :
    Dot,            // .
    Comma,          // ,
    Semicolon,      // ;
    At,             // @
    Hash,           // #
    HashExport,     // #export
    HashIf,         // #if
    HashElse,       // #else

    // Delimiters
    LParen,         // (
    RParen,         // )
    LBrace,         // {
    RBrace,         // }
    LBracket,       // [
    RBracket,       // ]

    // Special
    Eof,
    Newline,        // significant in some contexts
    Error,
};

struct Token {
    TokenType type;
    SourceSpan span;
    std::string lexeme;
    std::variant<std::monostate, std::int64_t, double, std::string> value;

    std::string to_string() const;
};

const char* token_type_name(TokenType type);
bool is_keyword(TokenType type);
bool is_builtin_type(TokenType type);

} // namespace femto
