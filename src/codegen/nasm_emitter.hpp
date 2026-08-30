#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "frontend/ast.hpp"
#include "sema/type_checker.hpp"

namespace femto {

enum class TargetOS {
    Linux,
    Windows
};

struct LoopContext {
    std::string continue_label;
    std::string break_label;
};

struct VarInfo {
    uint32_t stack_offset = 0;
    SemaType* type = nullptr;
    bool in_register = false;
    std::string reg_name;      // e.g. "r12", "r12d", "xmm6"
    std::string reg_name_full; // 64-bit base register name e.g. "r12"
    bool is_float_reg = false;
};

struct VarUsageStats {
    std::string name;
    SemaType* type = nullptr;
    uint32_t weight = 0;
    bool address_taken = false;
    bool is_param = false;
    size_t param_idx = 0;
};

class NasmEmitter {
public:
    explicit NasmEmitter(const std::unordered_map<std::string, SemaType*>& type_env,
                         const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& enum_defs,
                         const std::unordered_map<std::string, int64_t>& const_defs,
                         const std::unordered_map<std::string, double>& float_const_defs,
                         bool enable_bounds_checks = true,
                         TargetOS target_os = TargetOS::Linux)
        : type_env_(type_env), enum_defs_(enum_defs), const_defs_(const_defs),
          float_const_defs_(float_const_defs), enable_bounds_checks_(enable_bounds_checks),
          target_os_(target_os) {}

    std::string generate_assembly(const ASTProgram& program);

private:
    const std::unordered_map<std::string, SemaType*>& type_env_;
    const std::unordered_map<std::string, std::unordered_map<std::string, int64_t>>& enum_defs_;
    const std::unordered_map<std::string, int64_t>& const_defs_;
    const std::unordered_map<std::string, double>& float_const_defs_;
    bool enable_bounds_checks_ = true;
    TargetOS target_os_ = TargetOS::Linux;
    std::stringstream text_sec_;
    std::stringstream rodata_sec_;
    std::stringstream data_sec_;
    std::unordered_map<std::string, VarInfo> local_vars_;
    std::vector<LoopContext> loop_stack_;
    std::vector<uint32_t> subject_stack_;
    std::vector<std::vector<const ASTStmt*>> defer_scopes_;
    uint32_t current_stack_offset_ = 0;
    uint32_t current_frame_size_ = 0;
    const ASTProgram* current_program_ = nullptr;
    uint64_t label_seq_ = 0;

    std::vector<std::string> used_saved_gp_regs_;
    std::vector<std::string> used_saved_xmm_regs_;

    std::string next_label(std::string_view prefix) {
        return std::string(prefix) + "_" + std::to_string(++label_seq_);
    }

    uint32_t calculate_function_stack_size(const ASTFunctionDecl* fn);
    int64_t eval_const_expr(const ASTExpr* expr);
    SemaType* resolve_type_node(ASTType* ty);
    bool is_float_expr(const ASTExpr* expr);
    bool is_128bit_expr(const ASTExpr* expr);
    bool is_64bit_expr(const ASTExpr* expr);
    bool is_string_expr(const ASTExpr* expr);
    bool is_slice_expr(const ASTExpr* expr);
    bool is_16byte_type(const SemaType* ty);
    bool is_16byte_expr(const ASTExpr* expr);
    uint64_t get_type_id(const ASTExpr* expr);
    SemaType* get_member_type(const ASTExpr* expr);
    SemaType* get_expr_type(const ASTExpr* expr);

    void analyze_variable_usage(const ASTFunctionDecl* fn, std::unordered_map<std::string, VarUsageStats>& stats);
    void perform_register_allocation(const ASTFunctionDecl* fn, const std::unordered_map<std::string, VarUsageStats>& stats);

    void emit_function(const ASTFunctionDecl* fn);
    void emit_epilogue_sequence();
    void emit_statement(const ASTStmt* stmt, uint32_t& stack_offset);
    void emit_deferred_statements(uint32_t& stack_offset);
    void emit_expression(const ASTExpr* expr);
    void emit_lvalue_address(const ASTExpr* lval);

    std::string optimize_assembly(const std::string& input);
};

} // namespace femto