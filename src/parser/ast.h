#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "common/source_location.h"
#include "lexer/token.h"

namespace femto::ast {

// Forward declarations
struct Expr;
struct Stmt;
struct TypeNode;
struct Decl;

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
using TypePtr = std::unique_ptr<TypeNode>;
using DeclPtr = std::unique_ptr<Decl>;

// ---- Type nodes ----

struct PrimitiveType {
    TokenType token_type;
};

struct PointerType {
    TypePtr inner;
};

struct SliceType {
    TypePtr inner;
};

struct ArrayType {
    TypePtr inner;
    ExprPtr size;
};

struct FunctionType {
    std::vector<TypePtr> param_types;
    TypePtr return_type;
    bool is_error_return = false; // !T
};

struct NamedType {
    std::string name;
    SourceSpan span;
};

struct GenericType {
    std::string name;
    std::vector<TypePtr> type_args;
    SourceSpan span;
};

struct TypeNode {
    SourceSpan span;
    std::variant<
        PrimitiveType,
        PointerType,
        SliceType,
        ArrayType,
        FunctionType,
        NamedType,
        GenericType
    > data;
};

// ---- Expressions ----

struct IntegerLit {
    std::int64_t value;
};

struct FloatLit {
    double value;
};

struct CharLit {
    std::string value;
};

struct StringLit {
    std::string value;
};

struct BoolLit {
    bool value;
};

struct NullLit {};

struct Identifier {
    std::string name;
    SourceSpan span;
};

struct BinaryExpr {
    enum Op {
        Add, Sub, Mul, Div, Mod,
        BitAnd, BitOr, BitXor, BitNot,
        LShift, RShift,
        Eq, Neq, Lt, Gt, Le, Ge,
        LogicAnd, LogicOr,
    };
    Op op;
    ExprPtr left;
    ExprPtr right;
};

struct UnaryExpr {
    enum Op {
        Neg, Not, BitNot, Deref, Addr, PreInc, PreDec,
    };
    Op op;
    ExprPtr operand;
};

struct PostfixExpr {
    enum Op {
        Inc, Dec,
    };
    Op op;
    ExprPtr operand;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
};

struct IndexExpr {
    ExprPtr object;
    ExprPtr index;
};

struct MemberExpr {
    ExprPtr object;
    std::string member;
    SourceSpan member_span;
};

struct CastExpr {
    TypePtr target_type;
    ExprPtr value;
    bool checked = false; // ! for checked cast
};

struct BitcastExpr {
    TypePtr target_type;
    ExprPtr value;
};

struct GenericCallExpr {
    ExprPtr callee;
    std::vector<TypePtr> type_args;
};

struct MatchExpr;
struct ResultBranchExpr;

struct ResultBranchExpr {
    ExprPtr expr;
    ExprPtr on_success;  // block expression
    ExprPtr on_failure;  // block expression
};

struct SuccessExpr {
    ExprPtr value;
};

struct FailureExpr {
    std::optional<ExprPtr> value;
};

struct ArrayLiteral {
    std::vector<ExprPtr> elements;
};

struct StructLiteral {
    std::string type_name;
    std::vector<std::pair<std::string, ExprPtr>> fields;
};

struct BuiltinExpr {
    std::string name; // @target, @sizeof, etc.
    SourceSpan span;
};

struct SizeofExpr {
    TypePtr type;
};

struct AlignofExpr {
    TypePtr type;
};

struct TypeofExpr {
    ExprPtr expr;
};

struct Expr {
    SourceSpan span;
    std::variant<
        IntegerLit,
        FloatLit,
        CharLit,
        StringLit,
        BoolLit,
        NullLit,
        Identifier,
        BinaryExpr,
        UnaryExpr,
        PostfixExpr,
        CallExpr,
        IndexExpr,
        MemberExpr,
        CastExpr,
        BitcastExpr,
        GenericCallExpr,
        ExprPtr,            // parenthesized expression
        ResultBranchExpr,
        SuccessExpr,
        FailureExpr,
        ArrayLiteral,
        StructLiteral,
        BuiltinExpr,
        SizeofExpr,
        AlignofExpr,
        TypeofExpr
    > data;
};

// ---- Statements ----

struct ExprStmt {
    ExprPtr expr;
};

struct AssignStmt {
    ExprPtr target;
    ExprPtr value;
};

struct CompoundAssignStmt {
    BinaryExpr::Op op;
    ExprPtr target;
    ExprPtr value;
};

struct ReturnStmt {
    std::optional<ExprPtr> value;
};

struct BreakStmt {
    std::uint32_t levels = 1;
};

struct ContinueStmt {
    std::uint32_t levels = 1;
};

struct IfStmt {
    ExprPtr condition;
    StmtPtr then_block;
    std::optional<StmtPtr> else_block;
};

struct WhileStmt {
    ExprPtr condition;
    StmtPtr body;
};

struct DoWhileStmt {
    StmtPtr body;
    ExprPtr condition;
};

struct CaseArm {
    ExprPtr condition;
    StmtPtr body;
};

struct SwitchStmt {
    ExprPtr subject;
    std::vector<CaseArm> cases;
    std::optional<StmtPtr> default_case;
};

struct MatchArm {
    ExprPtr condition;
    ExprPtr value;
};

struct MatchStmt {
    ExprPtr subject;
    std::vector<MatchArm> arms;
    std::optional<MatchExpr*> parent = nullptr;
};

struct ForeachStmt {
    std::optional<std::string> index_var;
    std::string element_var;
    TypePtr element_type;
    ExprPtr iterable;
    StmtPtr body;
};

struct Block {
    std::vector<StmtPtr> stmts;
};

struct Stmt {
    SourceSpan span;
    std::variant<
        ExprStmt,
        AssignStmt,
        CompoundAssignStmt,
        ReturnStmt,
        BreakStmt,
        ContinueStmt,
        IfStmt,
        WhileStmt,
        DoWhileStmt,
        SwitchStmt,
        MatchStmt,
        ForeachStmt,
        Block,
        DeclPtr
    > data;
};

// ---- Declarations ----

struct VariableDecl {
    std::string name;
    TypePtr type;
    ExprPtr init;
    bool is_const = false;
};

struct ConstantDecl {
    std::string name;
    TypePtr type;     // optional
    ExprPtr init;
};

struct Param {
    std::string name;
    TypePtr type;
    std::optional<ExprPtr> default_value;
    SourceSpan name_span;
};

struct FunctionDecl {
    std::string name;
    std::vector<TypePtr> generic_params;
    std::vector<Param> params;
    TypePtr return_type;
    bool is_error_return = false;
    StmtPtr body;       // nullptr for extern
    bool is_extern = false;
    std::string extern_abi;
};

struct StructField {
    std::string name;
    TypePtr type;
    std::optional<ExprPtr> default_value;
};

struct StructDecl {
    std::string name;
    std::vector<std::string> generic_params;
    std::vector<StructField> fields;
};

struct EnumVariant {
    std::string name;
    std::optional<ExprPtr> value;
};

struct EnumDecl {
    std::string name;
    TypePtr backing_type;
    std::vector<EnumVariant> variants;
};

struct UnionField {
    std::string name;
    TypePtr type;
};

struct UnionDecl {
    std::string name;
    std::vector<UnionField> fields;
};

struct NamespaceDecl {
    std::string name;
    std::vector<DeclPtr> decls;
};

struct ExternBlock {
    std::string abi;
    std::vector<DeclPtr> decls;
};

struct ImportDecl {
    std::string path;
    std::optional<std::string> alias;
};

struct ExportDecl {
    DeclPtr inner;
};

struct CompileTimeIf {
    ExprPtr condition;
    std::vector<DeclPtr> then_decls;
    std::vector<DeclPtr> else_decls;
};

struct Decl {
    SourceSpan span;
    bool is_exported = false;
    std::variant<
        VariableDecl,
        ConstantDecl,
        FunctionDecl,
        StructDecl,
        EnumDecl,
        UnionDecl,
        NamespaceDecl,
        ExternBlock,
        ImportDecl,
        ExportDecl,
        CompileTimeIf
    > data;
};

// ---- Top-level module ----

struct Module {
    std::string filename;
    std::vector<DeclPtr> decls;
};

} // namespace femto::ast
