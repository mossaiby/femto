#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <optional>

#include "lexer/token.h"

namespace femto::sema {

enum class TypeKind {
    Void,
    Bool,
    Int,
    UInt,
    Float,
    Char,
    String,
    Pointer,
    Slice,
    Array,
    Function,
    Named,      // struct, enum, union
    Generic,    // unresolved type parameter
    Error,      // error recovery type
};

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct VoidType {};
struct BoolType { std::uint32_t bits; };

struct IntType { std::uint32_t bits; };
struct UIntType { std::uint32_t bits; };
struct FloatType { std::uint32_t bits; };
struct CharType { std::uint32_t bits; };
struct StringType { std::uint32_t bits; };

struct PointerType { TypePtr inner; };
struct SliceType { TypePtr inner; };
struct ArrayType { TypePtr inner; std::uint64_t size; };

struct FunctionType {
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_error_return = false;
};

struct NamedType {
    std::string name;
    std::vector<TypePtr> generic_args;
};

struct GenericType {
    std::string name;
};

struct ErrorType {};

struct Type {
    TypeKind kind;
    std::variant<
        VoidType,
        BoolType,
        IntType,
        UIntType,
        FloatType,
        CharType,
        StringType,
        PointerType,
        SliceType,
        ArrayType,
        FunctionType,
        NamedType,
        GenericType,
        ErrorType
    > data;
};

// Factory functions
TypePtr make_void();
TypePtr make_bool(std::uint32_t bits);
TypePtr make_int(std::uint32_t bits);
TypePtr make_uint(std::uint32_t bits);
TypePtr make_float(std::uint32_t bits);
TypePtr make_char(std::uint32_t bits);
TypePtr make_string(std::uint32_t bits);
TypePtr make_pointer(TypePtr inner);
TypePtr make_slice(TypePtr inner);
TypePtr make_array(TypePtr inner, std::uint64_t size);
TypePtr make_function(std::vector<TypePtr> params, TypePtr ret, bool is_error = false);
TypePtr make_named(const std::string& name, std::vector<TypePtr> args = {});
TypePtr make_generic(const std::string& name);
TypePtr make_error();

// Type queries
bool is_integer(const Type& t);
bool is_unsigned(const Type& t);
bool is_floating(const Type& t);
bool is_numeric(const Type& t);
bool is_error(const Type& t);
bool types_equal(const Type& a, const Type& b);
bool is_assignable(const Type& from, const Type& to);

std::string type_to_string(const Type& t);

} // namespace femto::sema
