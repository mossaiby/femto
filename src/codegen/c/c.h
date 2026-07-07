#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#include "lir/lir.h"
#include "sema/type_system.h"

namespace femto::codegen::c {

class CCodeGen {
public:
    std::string generate(lir::LIRModule& mod);

private:
    std::ostringstream out_;
    lir::LIRModule* mod_ = nullptr;
    std::unordered_map<std::string, lir::RegId> var_map_;  // var_name -> reg_id
    std::unordered_set<lir::RegId> used_regs_;              // regs seen as dest
    std::unordered_map<lir::RegId, lir::TypePtr> reg_def_type_; // last def type per reg
    lir::LIRFunction* current_func_ = nullptr; // current function being emitted

    void emit_runtime_stubs();
    void emit_function(lir::LIRFunction& func);
    void emit_instruction(lir::LIRInstruction& inst);

    std::string mangle_name(const std::string& name);
    std::string type_to_c_type(lir::TypePtr type);
    std::string var_name(lir::RegId id, lir::TypePtr type);
    int count_call_args(lir::LIRInstruction& inst);

    bool is_float_type(lir::TypePtr type);
    bool is_pointer_type(lir::TypePtr type);
};

} // namespace femto::codegen::c
