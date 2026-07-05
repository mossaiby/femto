#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <optional>

#include "sema/type_system.h"
#include "common/source_location.h"

namespace femto::hir {

using TypePtr = sema::TypePtr;

// HIR Value IDs for SSA-like form
using ValueId = std::uint32_t;

struct HIRExpr;
struct HIRStmt;
struct HIRDecl;
struct HIRBlock;

using ExprPtr = std::unique_ptr<HIRExpr>;
using StmtPtr = std::unique_ptr<HIRStmt>;
using DeclPtr = std::unique_ptr<HIRDecl>;

// ---- Expressions ----

struct IntLit {
    std::int64_t value;
    TypePtr type;
};

struct FloatLit {
    double value;
    TypePtr type;
};

struct StringLit {
    std::string value;
    TypePtr type;
};

struct BoolLit {
    bool value;
    TypePtr type;
};

struct NullLit {
    TypePtr type;
};

struct VarRef {
    std::string name;
    TypePtr type;
};

struct BinaryOp {
    enum Op { Add, Sub, Mul, Div, Mod, Eq, Neq, Lt, Gt, Le, Ge, BitAnd, BitOr, BitXor, LShift, RShift, LogicAnd, LogicOr };
    Op op;
    ExprPtr left;
    ExprPtr right;
    TypePtr type;
};

struct UnaryOp {
    enum Op { Neg, Not, BitNot, Deref, Addr };
    Op op;
    ExprPtr operand;
    TypePtr type;
};

struct CallOp {
    std::string callee_name;
    std::vector<ExprPtr> args;
    TypePtr return_type;
};

struct CastOp {
    ExprPtr value;
    TypePtr target_type;
};

struct MemberAccess {
    ExprPtr object;
    std::string member;
    TypePtr type;
};

struct IndexOp {
    ExprPtr object;
    ExprPtr index;
    TypePtr type;
};

struct HIRExpr {
    SourceSpan span;
    std::variant<
        IntLit, FloatLit, StringLit, BoolLit, NullLit,
        VarRef, BinaryOp, UnaryOp, CallOp, CastOp, MemberAccess, IndexOp
    > data;
};

// ---- Statements ----

struct AssignStmt {
    std::string target_name;
    ExprPtr value;
};

struct ExprStmt {
    ExprPtr expr;
};

struct ReturnStmt {
    std::optional<ExprPtr> value;
};

struct IfStmt {
    ExprPtr condition;
    std::unique_ptr<HIRBlock> then_block;
    std::unique_ptr<HIRBlock> else_block;
};

struct WhileStmt {
    ExprPtr condition;
    std::unique_ptr<HIRBlock> body;
};

struct Block {
    std::vector<StmtPtr> stmts;
};

struct HIRStmt {
    SourceSpan span;
    std::variant<
        AssignStmt, ExprStmt, ReturnStmt, IfStmt, WhileStmt, Block
    > data;
};

// ---- Declarations ----

struct HIRFunction {
    std::string name;
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr return_type;
    bool is_error_return = false;
    std::unique_ptr<HIRBlock> body;
};

struct HIRStruct {
    std::string name;
    std::vector<std::pair<std::string, TypePtr>> fields;
};

struct HIREnum {
    std::string name;
    std::vector<std::pair<std::string, std::int64_t>> variants;
    TypePtr backing_type;
};

struct HIRConstant {
    std::string name;
    TypePtr type;
    ExprPtr init;
};

struct HIRDecl {
    SourceSpan span;
    std::variant<HIRFunction, HIRStruct, HIREnum, HIRConstant> data;
};

struct HIRBlock {
    std::vector<StmtPtr> stmts;
};

// ---- Module ----

struct HIRModule {
    std::string filename;
    std::vector<DeclPtr> decls;
};

} // namespace femto::hir
