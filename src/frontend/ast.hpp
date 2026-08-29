#pragma once
#include <string_view>
#include <vector>
#include <memory>
#include <variant>
#include <optional>
#include "common/source_manager.hpp"
#include "frontend/token.hpp"

namespace femto {

struct ASTType;
struct ASTExpr;
struct ASTStmt;
struct ASTDecl;

enum class TypeKind {
    Primitive, Pointer, Array, Slice, Result, Custom
};

struct ASTType {
    TypeKind kind;
    SourceSpan span;
    TokenKind primitive_kind;
    ASTType* pointee_or_element = nullptr;
    uint64_t array_size = 0;
    std::string_view custom_name;
    std::vector<ASTType*> generic_args;
};

enum class ExprKind {
    Literal, Identifier, Binary, Unary, PostfixUnwrap,
    Cast, LossyCast, Bitcast, Match, Call, MemberAccess,
    Index, SliceSubrange, StructLiteral, ArrayLiteral, Subject,
    BuiltinSizeof, BuiltinAlignof, BuiltinBitcast, BuiltinCast
};

struct ASTMatchArm {
    ASTExpr* condition;
    std::vector<ASTStmt*> statements;
    ASTExpr* result_expr;
    SourceSpan span;
};

struct ASTExpr {
    ExprKind kind;
    SourceSpan span;
    std::string_view raw_text;
    
    ASTExpr* left = nullptr;
    ASTExpr* right = nullptr;
    std::string_view op;
    ASTType* target_type = nullptr;
    std::vector<ASTType*> generic_args;
    std::vector<ASTExpr*> args;
    std::vector<std::pair<std::string_view, ASTExpr*>> struct_fields;
    std::vector<ASTMatchArm> match_arms;
};

enum class StmtKind {
    VarDecl, ConstDecl, Assignment, CompoundAssignment,
    Increment, Decrement, If, While, DoWhile, For, Switch,
    Foreach, Break, Continue, Return, ResultBranch, ExprStmt,
    HashIf
};

struct ASTSwitchCase {
    ASTExpr* match_val;
    std::vector<ASTStmt*> body;
    SourceSpan span;
};

struct ASTStmt {
    StmtKind kind;
    SourceSpan span;

    // Declarations & Assignments
    std::string_view name;
    ASTType* type_annot = nullptr;
    ASTExpr* init_expr = nullptr;
    bool is_const = false;

    ASTExpr* target_expr = nullptr;
    ASTExpr* value_expr = nullptr;
    std::string_view op;

    // Control Flow
    ASTExpr* condition = nullptr;
    std::vector<ASTStmt*> then_block;
    std::vector<ASTStmt*> else_block;
    std::vector<ASTSwitchCase> switch_cases;

    // For Loop (3-clause)
    ASTStmt* init_stmt = nullptr;
    ASTStmt* step_stmt = nullptr;

    // Foreach
    std::string_view iter_idx;
    std::string_view iter_var;
    ASTType* iter_type = nullptr;
    ASTExpr* iter_collection = nullptr;

    uint32_t loop_levels = 1;

    // Result Branch
    std::string_view success_var;
    std::vector<ASTStmt*> success_block;
    std::string_view failure_var;
    std::vector<ASTStmt*> failure_block;
};

struct ASTParam {
    std::string_view name;
    ASTType* type;
    ASTExpr* default_value;
    SourceSpan span;
};

struct ASTFunctionDecl {
    std::string_view name;
    std::vector<std::string_view> generic_params;
    std::vector<ASTParam> params;
    ASTType* return_type;
    std::vector<ASTStmt*> body;
    bool is_exported = false;
    bool is_extern_c = false;
    SourceSpan span;
};

struct ASTStructField {
    std::string_view name;
    ASTType* type;
    ASTExpr* default_value;
    SourceSpan span;
};

struct ASTStructDecl {
    std::string_view name;
    std::vector<std::string_view> generic_params;
    std::vector<ASTStructField> fields;
    bool is_exported = false;
    SourceSpan span;
};

struct ASTUnionField {
    std::string_view name;
    ASTType* type;
    ASTExpr* default_value;
    SourceSpan span;
};

struct ASTUnionDecl {
    std::string_view name;
    std::vector<std::string_view> generic_params;
    std::vector<ASTUnionField> fields;
    bool is_exported = false;
    SourceSpan span;
};

struct ASTEnumVariant {
    std::string_view name;
    std::optional<int64_t> value;
    SourceSpan span;
};

struct ASTEnumDecl {
    std::string_view name;
    ASTType* backing_type;
    std::vector<ASTEnumVariant> variants;
    bool is_exported = false;
    SourceSpan span;
};

struct ASTConstDecl {
    std::string_view name;
    ASTType* type_annot = nullptr;
    ASTExpr* init_expr = nullptr;
    bool is_exported = false;
    SourceSpan span;
};

struct ASTProgram {
    std::vector<std::string> imports;
    std::vector<ASTFunctionDecl*> functions;
    std::vector<ASTStructDecl*> structs;
    std::vector<ASTUnionDecl*> unions;
    std::vector<ASTEnumDecl*> enums;
    std::vector<ASTConstDecl*> constants;
};

} // namespace femto