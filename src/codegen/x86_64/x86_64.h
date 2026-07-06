#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>

#include "lir/lir.h"

namespace femto::codegen::x86_64 {

class X86_64CodeGen {
public:
    std::string generate(lir::LIRModule& mod);

private:
    std::ostringstream out_;
    int string_counter_ = 0;
    int float_counter_ = 0;
    std::unordered_map<std::string, std::string> string_labels_;
    std::vector<std::pair<std::string, std::string>> string_data_; // label -> value
    std::vector<std::pair<std::string, double>> float_data_;       // label -> value
    std::vector<std::pair<std::string, float>> float32_data_;      // label -> value

    void emit_prologue(lir::LIRFunction& func);
    void emit_epilogue(lir::LIRFunction& func);
    void emit_instruction(lir::LIRInstruction& inst, lir::LIRFunction& func);
    void emit_function(lir::LIRFunction& func);
    void emit_data_section(lir::LIRModule& mod);
    void emit_runtime_stubs();
    void emit_entry_point();

    std::string reg_name(lir::VirtualReg reg);
    std::string xmm_name(lir::VirtualReg reg);
    std::string new_string_label(const std::string& value);
    std::string get_or_create_string_label(const std::string& value);
    std::string new_float_label(double value);
    std::string new_float32_label(float value);

    bool is_float_type(lir::TypePtr type);
    bool is_float64_type(lir::TypePtr type);
    bool is_float32_type(lir::TypePtr type);

    // Register allocation (simple linear scan)
    std::unordered_map<lir::RegId, std::string> reg_map_;
    std::unordered_map<lir::RegId, std::string> xmm_map_;
    std::unordered_map<std::string, std::pair<std::string, lir::TypePtr>> var_map_; // name -> (stack_slot, type)
    std::vector<std::string> param_regs_ = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    std::vector<std::string> xmm_param_regs_ = {"xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
    std::vector<std::string> callee_saved_ = {"rbx", "r12", "r13", "r14", "r15"};
    int stack_offset_ = 0;
    int xmm_stack_offset_ = 0;
    lir::LIRModule* current_module_ = nullptr;
};

} // namespace femto::codegen::x86_64
