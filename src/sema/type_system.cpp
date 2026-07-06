#include "type_system.h"
#include "parser/ast.h"

#include <sstream>
#include <variant>

namespace femto::sema {

TypePtr make_void() { return std::make_shared<Type>(TypeKind::Void, VoidType{}); }
TypePtr make_bool(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::Bool, BoolType{bits}); }
TypePtr make_int(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::Int, IntType{bits}); }
TypePtr make_uint(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::UInt, UIntType{bits}); }
TypePtr make_float(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::Float, FloatType{bits}); }
TypePtr make_char(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::Char, CharType{bits}); }
TypePtr make_string(std::uint32_t bits) { return std::make_shared<Type>(TypeKind::String, StringType{bits}); }
TypePtr make_pointer(TypePtr inner) { return std::make_shared<Type>(TypeKind::Pointer, PointerType{std::move(inner)}); }
TypePtr make_slice(TypePtr inner) { return std::make_shared<Type>(TypeKind::Slice, SliceType{std::move(inner)}); }
TypePtr make_array(TypePtr inner, std::uint64_t size) { return std::make_shared<Type>(TypeKind::Array, ArrayType{std::move(inner), size}); }
TypePtr make_function(std::vector<TypePtr> params, TypePtr ret, bool is_error) {
    return std::make_shared<Type>(TypeKind::Function, FunctionType{std::move(params), std::move(ret), is_error});
}
TypePtr make_named(const std::string& name, std::vector<TypePtr> args) {
    return std::make_shared<Type>(TypeKind::Named, NamedType{name, std::move(args)});
}
TypePtr make_generic(const std::string& name) { return std::make_shared<Type>(TypeKind::Generic, GenericType{name}); }
TypePtr make_error() { return std::make_shared<Type>(TypeKind::Error, ErrorType{}); }

bool is_integer(const Type& t) { return t.kind == TypeKind::Int || t.kind == TypeKind::UInt; }
bool is_unsigned(const Type& t) { return t.kind == TypeKind::UInt; }
bool is_floating(const Type& t) { return t.kind == TypeKind::Float; }
bool is_numeric(const Type& t) { return is_integer(t) || is_floating(t); }
bool is_error(const Type& t) { return t.kind == TypeKind::Error; }

bool types_equal(const Type& a, const Type& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case TypeKind::Void: return true;
        case TypeKind::Error: return true;
        case TypeKind::Bool: return std::get<BoolType>(a.data).bits == std::get<BoolType>(b.data).bits;
        case TypeKind::Int: return std::get<IntType>(a.data).bits == std::get<IntType>(b.data).bits;
        case TypeKind::UInt: return std::get<UIntType>(a.data).bits == std::get<UIntType>(b.data).bits;
        case TypeKind::Float: return std::get<FloatType>(a.data).bits == std::get<FloatType>(b.data).bits;
        case TypeKind::Char: return std::get<CharType>(a.data).bits == std::get<CharType>(b.data).bits;
        case TypeKind::String: return std::get<StringType>(a.data).bits == std::get<StringType>(b.data).bits;
        case TypeKind::Pointer: return types_equal(*std::get<PointerType>(a.data).inner, *std::get<PointerType>(b.data).inner);
        case TypeKind::Slice: return types_equal(*std::get<SliceType>(a.data).inner, *std::get<SliceType>(b.data).inner);
        case TypeKind::Named: return std::get<NamedType>(a.data).name == std::get<NamedType>(b.data).name;
        case TypeKind::Generic: return std::get<GenericType>(a.data).name == std::get<GenericType>(b.data).name;
        case TypeKind::Function: {
            auto& fa = std::get<FunctionType>(a.data);
            auto& fb = std::get<FunctionType>(b.data);
            if (fa.param_types.size() != fb.param_types.size()) return false;
            if (fa.is_error_return != fb.is_error_return) return false;
            for (std::size_t i = 0; i < fa.param_types.size(); ++i) {
                if (!types_equal(*fa.param_types[i], *fb.param_types[i])) return false;
            }
            return types_equal(*fa.return_type, *fb.return_type);
        }
        case TypeKind::Array: {
            auto& aa = std::get<ArrayType>(a.data);
            auto& ab = std::get<ArrayType>(b.data);
            return aa.size == ab.size && types_equal(*aa.inner, *ab.inner);
        }
    }
    return false;
}

bool is_assignable(const Type& from, const Type& to) {
    if (is_error(from) || is_error(to)) return true;
    if (types_equal(from, to)) return true;
    // Numeric literal coercion: int literals can coerce to any numeric type
    if (is_integer(from) && is_numeric(to)) return true;
    if (is_floating(from) && is_floating(to)) return true;
    // Pointer null coercion: null pointer to any pointer
    if (from.kind == TypeKind::Pointer && to.kind == TypeKind::Pointer) return true;
    // Array/Slice target: check element type compatibility
    if (to.kind == TypeKind::Slice) {
        return is_assignable(from, *std::get<SliceType>(to.data).inner);
    }
    if (to.kind == TypeKind::Array) {
        return is_assignable(from, *std::get<ArrayType>(to.data).inner);
    }
    return false;
}

std::string type_to_string(const Type& t) {
    switch (t.kind) {
        case TypeKind::Void: return "void";
        case TypeKind::Error: return "<error>";
        case TypeKind::Bool: return "bool" + std::to_string(std::get<BoolType>(t.data).bits);
        case TypeKind::Int: return "int" + std::to_string(std::get<IntType>(t.data).bits);
        case TypeKind::UInt: return "uint" + std::to_string(std::get<UIntType>(t.data).bits);
        case TypeKind::Float: return "float" + std::to_string(std::get<FloatType>(t.data).bits);
        case TypeKind::Char: return "char" + std::to_string(std::get<CharType>(t.data).bits);
        case TypeKind::String: return "string" + std::to_string(std::get<StringType>(t.data).bits);
        case TypeKind::Pointer: return type_to_string(*std::get<PointerType>(t.data).inner) + "*";
        case TypeKind::Slice: return type_to_string(*std::get<SliceType>(t.data).inner) + "[]";
        case TypeKind::Named: return std::get<NamedType>(t.data).name;
        case TypeKind::Generic: return "<" + std::get<GenericType>(t.data).name + ">";
        case TypeKind::Array:
            return type_to_string(*std::get<ArrayType>(t.data).inner) +
                   "[" + std::to_string(std::get<ArrayType>(t.data).size) + "]";
        case TypeKind::Function: {
            auto& ft = std::get<FunctionType>(t.data);
            std::ostringstream os;
            os << "(";
            for (std::size_t i = 0; i < ft.param_types.size(); ++i) {
                if (i > 0) os << ", ";
                os << type_to_string(*ft.param_types[i]);
            }
            os << ") -> ";
            if (ft.is_error_return) os << "!";
            os << type_to_string(*ft.return_type);
            return os.str();
        }
    }
    return "<unknown>";
}

TypePtr resolve_ast_type(const ast::TypeNode& node) {
    return std::visit([](const auto& t) -> TypePtr {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, ast::PrimitiveType>) {
            switch (t.token_type) {
                case TokenType::TY_int8: return make_int(8);
                case TokenType::TY_int16: return make_int(16);
                case TokenType::TY_int32: return make_int(32);
                case TokenType::TY_int64: return make_int(64);
                case TokenType::TY_int128: return make_int(128);
                case TokenType::TY_int256: return make_int(256);
                case TokenType::TY_int512: return make_int(512);
                case TokenType::TY_uint8: return make_uint(8);
                case TokenType::TY_uint16: return make_uint(16);
                case TokenType::TY_uint32: return make_uint(32);
                case TokenType::TY_uint64: return make_uint(64);
                case TokenType::TY_uint128: return make_uint(128);
                case TokenType::TY_uint256: return make_uint(256);
                case TokenType::TY_uint512: return make_uint(512);
                case TokenType::TY_float16: return make_float(16);
                case TokenType::TY_float32: return make_float(32);
                case TokenType::TY_float64: return make_float(64);
                case TokenType::TY_float128: return make_float(128);
                case TokenType::TY_bool8: return make_bool(8);
                case TokenType::TY_bool16: return make_bool(16);
                case TokenType::TY_bool32: return make_bool(32);
                case TokenType::TY_bool64: return make_bool(64);
                case TokenType::TY_bool128: return make_bool(128);
                case TokenType::TY_bool256: return make_bool(256);
                case TokenType::TY_bool512: return make_bool(512);
                case TokenType::TY_char8: return make_char(8);
                case TokenType::TY_char16: return make_char(16);
                case TokenType::TY_char32: return make_char(32);
                case TokenType::TY_string8: return make_string(8);
                case TokenType::TY_string16: return make_string(16);
                case TokenType::TY_string32: return make_string(32);
                case TokenType::KW_void: return make_void();
                default: return make_error();
            }
        } else if constexpr (std::is_same_v<T, ast::PointerType>) {
            return make_pointer(resolve_ast_type(*t.inner));
        } else if constexpr (std::is_same_v<T, ast::SliceType>) {
            return make_slice(resolve_ast_type(*t.inner));
        } else if constexpr (std::is_same_v<T, ast::ArrayType>) {
            uint64_t size = 0;
            if (t.size) {
                if (auto* lit = std::get_if<ast::IntegerLit>(&t.size->data)) {
                    size = static_cast<uint64_t>(lit->value);
                }
            }
            return make_array(resolve_ast_type(*t.inner), size);
        } else if constexpr (std::is_same_v<T, ast::FunctionType>) {
            std::vector<TypePtr> params;
            for (auto& pt : t.param_types) {
                params.push_back(resolve_ast_type(*pt));
            }
            TypePtr ret = t.return_type ? resolve_ast_type(*t.return_type) : make_void();
            return make_function(std::move(params), ret, t.is_error_return);
        } else if constexpr (std::is_same_v<T, ast::NamedType>) {
            return make_named(t.name);
        } else if constexpr (std::is_same_v<T, ast::GenericType>) {
            return make_generic(t.name);
        } else {
            return make_error();
        }
    }, node.data);
}

} // namespace femto::sema
