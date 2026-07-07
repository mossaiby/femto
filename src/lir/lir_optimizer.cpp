#include "lir_optimizer.h"

#include <unordered_set>

namespace femto::lir {

void LIROptimizer::optimize(LIRModule& mod) {
    for (auto& func : mod.functions) {
        dead_code_elimination(func, mod);
    }
    for (auto& func : mod.functions) {
        for (auto& block : func.blocks) {
            peephole_optimize(block);
        }
    }
}

static int count_args_for_call(const LIRInstruction& inst, const LIRModule& mod) {
    for (auto& f : mod.functions) {
        if (f.name == inst.label)
            return static_cast<int>(f.params.size());
    }
    // Known runtime stubs
    static const struct { const char* name; int count; } stubs[] = {
        {"std.io.print", 1}, {"std.io.println", 1}, {"__builtin_exit", 1},
        {"std.string8.from_int", 1}, {"std.string8.from_float", 1},
    };
    for (auto& s : stubs) {
        if (inst.label == s.name) return s.count;
    }
    return 1;
}

void LIROptimizer::dead_code_elimination(LIRFunction& func, const LIRModule& mod) {
    // Remove instructions whose dest is never used
    for (auto& block : func.blocks) {
        std::unordered_set<RegId> used_regs;
        // First pass: find all used registers
        for (auto& inst : block.instructions) {
            if (inst.src1.id != 0) used_regs.insert(inst.src1.id);
            if (inst.src2.id != 0) used_regs.insert(inst.src2.id);
            // Call instructions use consecutive registers from src1 for args
            if (inst.opcode == LIROpcode::Call) {
                int nargs = count_args_for_call(inst, mod);
                for (int i = 0; i < nargs; ++i) {
                    RegId aid = inst.src1.id + static_cast<RegId>(i);
                    if (aid != 0) used_regs.insert(aid);
                }
            }
        }
        // Second pass: remove instructions whose dest is unused
        std::vector<LIRInstruction> kept;
        for (auto& inst : block.instructions) {
            if (inst.opcode == LIROpcode::Ret ||
                inst.opcode == LIROpcode::Jump ||
                inst.opcode == LIROpcode::Branch ||
                inst.opcode == LIROpcode::Call ||
                inst.opcode == LIROpcode::Store ||
                inst.opcode == LIROpcode::Label ||
                inst.opcode == LIROpcode::Nop ||
                inst.opcode == LIROpcode::Phi) {
                kept.push_back(std::move(inst));
            } else if (!inst.var_name.empty()) {
                // Move with variable name - keep it (variable assignment)
                kept.push_back(std::move(inst));
            } else if (inst.dest.id != 0 && used_regs.count(inst.dest.id) == 0) {
                // Dead code - skip it
            } else {
                kept.push_back(std::move(inst));
            }
        }
        block.instructions = std::move(kept);
    }
}

void LIROptimizer::peephole_optimize(LIRBasicBlock& block) {
    std::vector<LIRInstruction> optimized;
    for (auto& inst : block.instructions) {
        // Skip nops
        if (inst.opcode == LIROpcode::Nop) continue;
        optimized.push_back(std::move(inst));
    }
    block.instructions = std::move(optimized);
}

} // namespace femto::lir
