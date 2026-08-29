#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include "frontend/ast.hpp"
#include "common/diagnostic.hpp"
#include "common/arena.hpp"

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

struct UnionFieldInfo {
    std::string name;
    SemaType* type;
    uint32_t offset = 0;
};

struct UnionTypeInfo {
    std::string name;
    std::vector<UnionFieldInfo> fields;
    std::unordered_map<std::string, size_t> field_map;
};

struct SemaType {
    enum class Kind { Primitive, Pointer, Array, Slice, Result, Struct, Union } kind;
    uint32_t size_bytes = 0;
    uint32_t align_bytes = 0;
    std::variant<PrimitiveTypeInfo, PointerTypeInfo, ArrayTypeInfo, SliceTypeInfo, ResultTypeInfo, StructTypeInfo, UnionTypeInfo> data;

    bool is_integer() const {
        if (kind != Kind::Primitive) return false;
        auto pk = std::get<PrimitiveTypeInfo>(data).kind;
        return (pk >= TokenKind::KwInt8 && pk <= TokenKind::KwUint512);
    }

    bool is_floating_point() const {
        if (kind != Kind::Primitive) return false;
        auto pk = std::get<PrimitiveTypeInfo>(data).kind;
        return (pk == TokenKind::KwFloat16 || pk == TokenKind::KwFloat32 || pk == TokenKind::KwFloat64 || pk == TokenKind::KwFloat128);
    }
};

class TypeChecker {
public:
    TypeChecker(Arena& arena, Diagnostics& diag) : arena_(arena), diag_(diag) {}

    bool check_program(ASTProgram& prog);

    SemaType* resolve_ast_type(ASTType* ast_ty);
    bool check_type_compatibility(SemaType* expected, SemaType* actual, SourceSpan span);

    const std::unordered_map<std::string, SemaType*>& type_env() const { return type_env_; }
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& enum_defs() const { return enum_defs_; }
    const std::unordered_map<std::string, int64_t>& const_defs() const { return const_defs_; }

    static std::string get_type_name(const ASTType* ty);

private:
    Arena& arena_;
    Diagnostics& diag_;
    std::unordered_map<std::string, SemaType*> type_env_;
    std::unordered_map<std::string, std::unordered_map<std::string, int64_t>> enum_defs_;
    std::unordered_map<std::string, int64_t> const_defs_;
    std::unordered_map<std::string, SemaType*> symbol_table_;
    std::unordered_map<std::string, ASTStructDecl*> generic_structs_;
    std::unordered_map<std::string, ASTUnionDecl*> generic_unions_;
    std::unordered_map<std::string, ASTFunctionDecl*> generic_functions_;
    uint32_t current_loop_depth_ = 0;

    void init_primitives();
    void compute_struct_layout(ASTStructDecl* decl, std::string custom_name = "");
    void compute_union_layout(ASTUnionDecl* decl, std::string custom_name = "");
    void check_statement(ASTStmt* stmt, SemaType* return_type, ASTProgram& prog);
    SemaType* check_expression(ASTExpr* expr, ASTProgram& prog);

    int64_t eval_const_expr(ASTExpr* expr);

    // Monomorphization
    std::string monomorphize_struct(ASTType* generic_ty);
    std::string monomorphize_union(ASTType* generic_ty);
    std::string monomorphize_function(ASTExpr* call_expr, ASTProgram& prog);
    ASTType* clone_and_substitute_type(ASTType* ty, const std::unordered_map<std::string, ASTType*>& subst);
    ASTExpr* clone_and_substitute_expr(ASTExpr* expr, const std::unordered_map<std::string, ASTType*>& subst);
    ASTStmt* clone_and_substitute_stmt(ASTStmt* stmt, const std::unordered_map<std::string, ASTType*>& subst);
};

} // namespace femto