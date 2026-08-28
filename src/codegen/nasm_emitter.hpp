#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include "frontend/ast.hpp"
#include "sema/type_checker.hpp"

namespace femto {

struct LoopContext {
    std::string continue_label;
    std::string break_label;
};

struct VarInfo {
    uint32_t stack_offset = 0;
    SemaType* type = nullptr;
};

class NasmEmitter {
public:
    explicit NasmEmitter(const std::unordered_map<std::string, SemaType*>& type_env,
                         const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& enum_defs,
                         const std::unordered_map<std::string, int64_t>& const_defs)
        : type_env_(type_env), enum_defs_(enum_defs), const_defs_(const_defs) {}

    std::string generate_assembly(const ASTProgram& program);

private:
    const std::unordered_map<std::string, SemaType*>& type_env_;
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& enum_defs_;
    const std::unordered_map<std::string, int64_t>& const_defs_;
    std::stringstream text_sec_;
    std::stringstream rodata_sec_;
    std::stringstream data_sec_;
    std::unordered_map<std::string, VarInfo> local_vars_;
    std::vector<LoopContext> loop_stack_;
    std::vector<uint32_t> subject_stack_;
    uint64_t label_seq_ = 0;

    std::string next_label(std::string_view prefix) {
        return std::string(prefix) + "_" + std::to_string(++label_seq_);
    }

    int64_t eval_const_expr(const ASTExpr* expr);
    SemaType* resolve_type_node(ASTType* ty);

    void emit_function(const ASTFunctionDecl* fn);
    void emit_statement(const ASTStmt* stmt, uint32_t& stack_offset);
    void emit_expression(const ASTExpr* expr);
    void emit_lvalue_address(const ASTExpr* lval);
};

} // namespace femto