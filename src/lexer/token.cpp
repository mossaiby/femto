#include "token.h"

#include <sstream>

namespace femto {

std::string Token::to_string() const {
    std::ostringstream oss;
    oss << "[" << token_type_name(type) << " " << span.start.line << ":" << span.start.column;
    if (!lexeme.empty()) {
        oss << " \"" << lexeme << "\"";
    }
    if (auto* int_val = std::get_if<std::int64_t>(&value)) {
        oss << " int=" << *int_val;
    } else if (auto* float_val = std::get_if<double>(&value)) {
        oss << " float=" << *float_val;
    } else if (auto* str_val = std::get_if<std::string>(&value)) {
        if (!str_val->empty()) oss << " str=\"" << *str_val << "\"";
    }
    oss << "]";
    return oss.str();
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::IntegerLiteral:    return "IntegerLiteral";
        case TokenType::FloatLiteral:      return "FloatLiteral";
        case TokenType::CharLiteral:       return "CharLiteral";
        case TokenType::StringLiteral:     return "StringLiteral";
        case TokenType::BoolLiteral:       return "BoolLiteral";
        case TokenType::Identifier:        return "Identifier";
        case TokenType::KW_import:         return "import";
        case TokenType::KW_for:            return "for";
        case TokenType::KW_while:          return "while";
        case TokenType::KW_do:             return "do";
        case TokenType::KW_if:             return "if";
        case TokenType::KW_then:           return "then";
        case TokenType::KW_else:           return "else";
        case TokenType::KW_switch:         return "switch";
        case TokenType::KW_case:           return "case";
        case TokenType::KW_match:          return "match";
        case TokenType::KW_return:         return "return";
        case TokenType::KW_break:          return "break";
        case TokenType::KW_continue:       return "continue";
        case TokenType::KW_enum:           return "enum";
        case TokenType::KW_struct:         return "struct";
        case TokenType::KW_null:           return "null";
        case TokenType::KW_extern:         return "extern";
        case TokenType::KW_foreach:        return "foreach";
        case TokenType::KW_array:          return "array";
        case TokenType::KW_namespace:      return "namespace";
        case TokenType::KW_success:        return "success";
        case TokenType::KW_failure:        return "failure";
        case TokenType::KW_true:           return "true";
        case TokenType::KW_false:          return "false";
        case TokenType::KW_void:           return "void";
        case TokenType::KW_in:             return "in";
        case TokenType::KW_default:        return "default";
        case TokenType::KW_const:          return "const";
        case TokenType::KW_union:          return "union";
        case TokenType::KW_class:          return "class";
        case TokenType::KW_interface:      return "interface";
        case TokenType::KW_defer:          return "defer";
        case TokenType::TY_int8:           return "int8";
        case TokenType::TY_int16:          return "int16";
        case TokenType::TY_int32:          return "int32";
        case TokenType::TY_int64:          return "int64";
        case TokenType::TY_int128:         return "int128";
        case TokenType::TY_int256:         return "int256";
        case TokenType::TY_int512:         return "int512";
        case TokenType::TY_uint8:          return "uint8";
        case TokenType::TY_uint16:         return "uint16";
        case TokenType::TY_uint32:         return "uint32";
        case TokenType::TY_uint64:         return "uint64";
        case TokenType::TY_uint128:        return "uint128";
        case TokenType::TY_uint256:        return "uint256";
        case TokenType::TY_uint512:        return "uint512";
        case TokenType::TY_float16:        return "float16";
        case TokenType::TY_float32:        return "float32";
        case TokenType::TY_float64:        return "float64";
        case TokenType::TY_float128:       return "float128";
        case TokenType::TY_bool8:          return "bool8";
        case TokenType::TY_bool16:         return "bool16";
        case TokenType::TY_bool32:         return "bool32";
        case TokenType::TY_bool64:         return "bool64";
        case TokenType::TY_bool128:        return "bool128";
        case TokenType::TY_bool256:        return "bool256";
        case TokenType::TY_bool512:        return "bool512";
        case TokenType::TY_char8:          return "char8";
        case TokenType::TY_char16:         return "char16";
        case TokenType::TY_char32:         return "char32";
        case TokenType::TY_string8:        return "string8";
        case TokenType::TY_string16:       return "string16";
        case TokenType::TY_string32:       return "string32";
        case TokenType::Plus:              return "+";
        case TokenType::Minus:             return "-";
        case TokenType::Star:              return "*";
        case TokenType::Slash:             return "/";
        case TokenType::Percent:           return "%";
        case TokenType::Amp:               return "&";
        case TokenType::Pipe:              return "|";
        case TokenType::Caret:             return "^";
        case TokenType::Tilde:             return "~";
        case TokenType::Bang:              return "!";
        case TokenType::Question:          return "?";
        case TokenType::DoubleQuestion:    return "??";
        case TokenType::AmpAmp:            return "&&";
        case TokenType::PipePipe:          return "||";
        case TokenType::EqEq:              return "==";
        case TokenType::Neq:               return "!=";
        case TokenType::Lt:                return "<";
        case TokenType::Gt:                return ">";
        case TokenType::Le:                return "<=";
        case TokenType::Ge:                return ">=";
        case TokenType::LShift:            return "<<";
        case TokenType::RShift:            return ">>";
        case TokenType::Eq:                return "=";
        case TokenType::PlusEq:            return "+=";
        case TokenType::MinusEq:           return "-=";
        case TokenType::StarEq:            return "*=";
        case TokenType::SlashEq:           return "/=";
        case TokenType::PercentEq:         return "%=";
        case TokenType::AmpEq:             return "&=";
        case TokenType::PipeEq:            return "|=";
        case TokenType::CaretEq:           return "^=";
        case TokenType::LShiftEq:          return "<<=";
        case TokenType::RShiftEq:          return ">>=";
        case TokenType::PlusPlus:          return "++";
        case TokenType::MinusMinus:        return "--";
        case TokenType::Arrow:             return "->";
        case TokenType::DoubleColon:       return "::";
        case TokenType::Colon:             return ":";
        case TokenType::Dot:               return ".";
        case TokenType::Comma:             return ",";
        case TokenType::Semicolon:         return ";";
        case TokenType::At:                return "@";
        case TokenType::Hash:              return "#";
        case TokenType::HashExport:        return "#export";
        case TokenType::HashIf:            return "#if";
        case TokenType::HashElse:          return "#else";
        case TokenType::LParen:            return "(";
        case TokenType::RParen:            return ")";
        case TokenType::LBrace:            return "{";
        case TokenType::RBrace:            return "}";
        case TokenType::LBracket:          return "[";
        case TokenType::RBracket:          return "]";
        case TokenType::Eof:              return "EOF";
        case TokenType::Newline:           return "Newline";
        case TokenType::Error:             return "Error";
    }
    return "Unknown";
}

bool is_keyword(TokenType type) {
    return type >= TokenType::KW_import && type <= TokenType::KW_defer;
}

bool is_builtin_type(TokenType type) {
    return type >= TokenType::TY_int8 && type <= TokenType::TY_string32;
}

} // namespace femto
