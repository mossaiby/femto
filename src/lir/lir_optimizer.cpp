#include "lir_optimizer.h"

#include <unordered_set>

namespace femto::lir {

void LIROptimizer::optimize(LIRModule& mod) {
    for (auto& func : mod.functions) {
        optimize_function(func);
    }
}

void LIROptimizer::optimize_function(LIRFunction& func) {
    dead_code_elimination(func);
    for (auto& block : func.blocks) {
        peephole_optimize(block);
    }
}

void LIROptimizer::dead_code_elimination(LIRFunction& func) {
    // Remove instructions whose dest is never used
    for (auto& block : func.blocks) {
        std::unordered_set<RegId> used_regs;
        // First pass: find all used registers (in src1, src2)
        for (auto& inst : block.instructions) {
            if (inst.src1.id != 0) used_regs.insert(inst.src1.id);
            if (inst.src2.id != 0) used_regs.insert(inst.src2.id);
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
        // Fold constant moves: Move dest, imm -> just store the imm
        if (inst.opcode == LIROpcode::Move && inst.imm != 0) {
            inst.opcode = LIROpcode::Nop;
        }
        // Skip nops
        if (inst.opcode == LIROpcode::Nop) continue;
        optimized.push_back(std::move(inst));
    }
    block.instructions = std::move(optimized);
}

} // namespace femto::lir
