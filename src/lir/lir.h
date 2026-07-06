#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <memory>

#include "sema/type_system.h"
#include "common/source_location.h"

namespace femto::lir {

using TypePtr = sema::TypePtr;
using RegId = std::uint32_t;

struct VirtualReg {
    RegId id;
    TypePtr type;
};

// LIR Instructions - three-address code style
struct LIROpcode {
    enum Enum {
        // Arithmetic
        Add, Sub, Mul, Div, Mod,
        // Bitwise
        And, Or, Xor, Not, Shl, Shr,
        // Comparison
        CmpEq, CmpNe, CmpLt, CmpGt, CmpLe, CmpGe,
        // Logical
        LogicAnd, LogicOr, LogicNot,
        // Memory
        Load, Store, LoadAddr,
        // Control flow
        Jump, Branch, Call, Ret,
        // Misc
        Move, Cast, Phi, Nop, Label
    };
};

struct LIRInstruction {
    LIROpcode::Enum opcode;
    VirtualReg dest;
    VirtualReg src1;
    VirtualReg src2;
    TypePtr type;
    std::string label;          // for labels and jumps
    std::string var_name;       // variable name for Move/Store targets
    std::int64_t imm = 0;      // for integer immediate values
    bool has_imm = false;      // distinguishes imm=0 from "no immediate"
    double float_imm = 0.0;    // for float immediate values
    bool has_float_imm = false; // true when float_imm is valid
};

struct LIRBasicBlock {
    std::string label;
    std::vector<LIRInstruction> instructions;
};

struct LIRFunction {
    std::string name;
    std::vector<std::pair<std::string, TypePtr>> params;
    TypePtr return_type;
    std::vector<LIRBasicBlock> blocks;
    std::uint32_t next_reg = 0;

    VirtualReg new_reg(TypePtr type) {
        return VirtualReg{next_reg++, type};
    }
};

struct LIRModule {
    std::string filename;
    std::vector<LIRFunction> functions;
    std::vector<std::pair<std::string, std::string>> string_data; // label -> value
};

} // namespace femto::lir
