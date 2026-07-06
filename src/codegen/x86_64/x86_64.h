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
    std::unordered_map<std::string, std::string> string_labels_;
    std::vector<std::pair<std::string, std::string>> string_data_; // label -> value

    void emit_prologue(lir::LIRFunction& func);
    void emit_epilogue(lir::LIRFunction& func);
    void emit_instruction(lir::LIRInstruction& inst, lir::LIRFunction& func);
    void emit_function(lir::LIRFunction& func);
    void emit_data_section(lir::LIRModule& mod);
    void emit_runtime_stubs();
    void emit_entry_point();

    std::string reg_name(lir::VirtualReg reg);
    std::string new_string_label(const std::string& value);
    std::string get_or_create_string_label(const std::string& value);

    // Register allocation (simple linear scan)
    std::unordered_map<lir::RegId, std::string> reg_map_;
    std::vector<std::string> param_regs_ = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    std::vector<std::string> callee_saved_ = {"rbx", "r12", "r13", "r14", "r15"};
    int stack_offset_ = 0;
};

} // namespace femto::codegen::x86_64
