#pragma once
#include <string_view>
#include <cstdint>
#include "common/source_manager.hpp"

namespace femto {

enum class TokenKind : uint16_t {
    Eof,
    // Literals
    IntLiteral, FloatLiteral, CharLiteral, StringLiteral, RawStringLiteral,
    Identifier,
    
    // Keywords
    KwImport, KwFor, KwWhile, KwDo, KwIf, KwThen, KwElse, KwSwitch, KwCase,
    KwMatch, KwReturn, KwBreak, KwContinue, KwEnum, KwStruct, KwNull, KwExtern,
    KwForeach, KwArray, KwNamespace, KwSuccess, KwFailure, KwTrue, KwFalse,
    KwVoid, KwIn, KwDefault, KwConst, KwUnion,
    
    // Type Keywords
    KwInt8, KwInt16, KwInt32, KwInt64, KwInt128, KwInt256, KwInt512,
    KwUint8, KwUint16, KwUint32, KwUint64, KwUint128, KwUint256, KwUint512,
    KwFloat16, KwFloat32, KwFloat64, KwFloat128,
    KwBool8, KwBool16, KwBool32, KwBool64, KwBool128, KwBool256, KwBool512,
    KwChar8, KwChar16, KwChar32,
    KwString8, KwString16, KwString32,

    // Builtins / Directives
    HashExport, HashIf, HashElse, HashSubject,
    AtBuiltin,

    // Operators & Delimiters
    DoubleColon, Colon, Semicolon, Comma, Dot, DotDot, Arrow,
    QuestionQuestion, Bang,
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Tilde,
    AmpAmp, PipePipe,
    Eq, EqEq, BangEq,
    Lt, LtEq, Gt, GtEq,
    Shl, Shr,
    PlusPlus, MinusMinus,
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,

    LParen, RParen,
    LBracket, RBracket,
    LBrace, RBrace
};

struct Token {
    TokenKind kind;
    SourceSpan span;
    std::string_view text;
};

} // namespace femto