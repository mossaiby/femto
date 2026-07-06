#pragma once

#include "lir.h"
#include "hir/hir.h"
#include "common/diagnostic.h"

namespace femto::lir {

class HIRToLIR {
public:
    HIRToLIR(DiagnosticEngine& diag);

    LIRModule lower(hir::HIRModule& hir);

private:
    DiagnosticEngine& diag_;
    int label_counter_ = 0;
    int string_counter_ = 0;
    LIRModule* current_mod_ = nullptr;

    std::string new_label(const std::string& prefix);

    void lower_function(hir::HIRFunction& func, LIRModule& mod);
    void lower_block(hir::HIRBlock& block, LIRFunction& lir_func);
    void lower_block(hir::HIRBlock& block, LIRFunction& lir_func, LIRBasicBlock& current_block);
    void lower_stmt(hir::HIRStmt& stmt, LIRFunction& lir_func, LIRBasicBlock& current_block);

    VirtualReg lower_expr(hir::HIRExpr& expr, LIRFunction& lir_func, LIRBasicBlock& current_block);

    void emit(LIRBasicBlock& block, LIROpcode::Enum op, VirtualReg dest = {0, nullptr},
              VirtualReg src1 = {0, nullptr}, VirtualReg src2 = {0, nullptr},
              TypePtr type = nullptr, const std::string& label = "", std::int64_t imm = 0,
              bool has_imm = false);

    // Map HIR BinaryOp to LIR opcode
    static LIROpcode::Enum map_binary_op(hir::BinaryOp::Op op);
};

} // namespace femto::lir
