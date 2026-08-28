#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include "frontend/ast.hpp"
#include "common/diagnostic.hpp"

namespace femto {

struct SemaType;

struct PrimitiveTypeInfo { TokenKind kind; };
struct PointerTypeInfo   { SemaType* pointee; };
struct ArrayTypeInfo     { SemaType* element; uint64_t size; };
struct SliceTypeInfo     { SemaType* element; };
struct ResultTypeInfo    { SemaType* payload; };

struct StructFieldInfo {
    std::string name;
    SemaType* type;
    uint32_t offset = 0;
};

struct StructTypeInfo {
    std::string name;
    std::vector<StructFieldInfo> fields;
    std::unordered_map<std::string, size_t> field_map;
};

struct SemaType {
    enum class Kind { Primitive, Pointer, Array, Slice, Result, Struct } kind;
    uint32_t size_bytes = 0;
    uint32_t align_bytes = 0;
    std::variant<PrimitiveTypeInfo, PointerTypeInfo, ArrayTypeInfo, SliceTypeInfo, ResultTypeInfo, StructTypeInfo> data;

    bool is_integer() const {
        if (kind != Kind::Primitive) return false;
        auto pk = std::get<PrimitiveTypeInfo>(data).kind;
        return (pk >= TokenKind::KwInt8 && pk <= TokenKind::KwUint512);
    }
};

class TypeChecker {
public:
    TypeChecker(Diagnostics& diag) : diag_(diag) {}

    bool check_program(ASTProgram& prog);

    SemaType* resolve_ast_type(ASTType* ast_ty);
    bool check_type_compatibility(SemaType* expected, SemaType* actual, SourceSpan span);

    const std::unordered_map<std::string, SemaType*>& type_env() const { return type_env_; }

private:
    Diagnostics& diag_;
    std::unordered_map<std::string, SemaType*> type_env_;
    std::unordered_map<std::string, SemaType*> symbol_table_;
    uint32_t current_loop_depth_ = 0;

    void init_primitives();
    void compute_struct_layout(ASTStructDecl* decl);
    void check_statement(ASTStmt* stmt, SemaType* return_type);
    SemaType* check_expression(ASTExpr* expr);
};

} // namespace femto