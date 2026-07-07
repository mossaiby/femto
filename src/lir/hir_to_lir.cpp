#include "hir_to_lir.h"

#include <sstream>

namespace femto::lir {

HIRToLIR::HIRToLIR(DiagnosticEngine& diag) : diag_(diag) {}

std::string HIRToLIR::new_label(const std::string& prefix) {
    return prefix + "." + std::to_string(label_counter_++);
}

LIRModule HIRToLIR::lower(hir::HIRModule& hir) {
    LIRModule mod;
    mod.filename = hir.filename;
    current_mod_ = &mod;

    for (auto& decl : hir.decls) {
        if (!decl) continue;
        if (auto* func = std::get_if<hir::HIRFunction>(&decl->data)) {
            lower_function(*func, mod);
        }
        // Structs, enums, constants are handled at compile time; no LIR needed
    }

    return mod;
}

void HIRToLIR::lower_function(hir::HIRFunction& func, LIRModule& mod) {
    var_reg_map_.clear();

    LIRFunction lir_func;
    lir_func.name = func.name;
    lir_func.return_type = func.return_type;

    for (auto& [name, type] : func.params) {
        lir_func.params.push_back({name, type});
    }

    // Expressions start after parameter register IDs to avoid stack slot collisions
    lir_func.next_reg = static_cast<std::uint32_t>(func.params.size());

    // Register parameters in the variable map
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        var_reg_map_[func.params[i].first] = VirtualReg{static_cast<std::uint32_t>(i), func.params[i].second};
    }

    if (func.body) {
        LIRBasicBlock entry_block;
        entry_block.label = "";
        lower_block(*func.body, lir_func, entry_block);
        lir_func.blocks.push_back(std::move(entry_block));
    }

    mod.functions.push_back(std::move(lir_func));
}

void HIRToLIR::lower_block(hir::HIRBlock& block, LIRFunction& lir_func) {
    LIRBasicBlock b;
    lower_block(block, lir_func, b);
    lir_func.blocks.push_back(std::move(b));
}

void HIRToLIR::lower_block(hir::HIRBlock& block, LIRFunction& lir_func, LIRBasicBlock& current_block) {
    for (auto& stmt : block.stmts) {
        lower_stmt(*stmt, lir_func, current_block);
    }
}

void HIRToLIR::lower_stmt(hir::HIRStmt& stmt, LIRFunction& lir_func, LIRBasicBlock& current_block) {
    std::visit([this, &lir_func, &current_block](auto& s) {
        using T = std::decay_t<decltype(s)>;

        if constexpr (std::is_same_v<T, hir::AssignStmt>) {
            auto val_reg = lower_expr(*s.value, lir_func, current_block);
            // Reuse existing register if variable was declared before
            auto it = var_reg_map_.find(s.target_name);
            VirtualReg target;
            if (it != var_reg_map_.end()) {
                target = it->second;
            } else {
                target = lir_func.new_reg(val_reg.type);
                var_reg_map_[s.target_name] = target;
            }
            emit_with_var(current_block, LIROpcode::Move, target, val_reg, {0, nullptr}, val_reg.type, s.target_name);
        } else if constexpr (std::is_same_v<T, hir::ExprStmt>) {
            lower_expr(*s.expr, lir_func, current_block);
        } else if constexpr (std::is_same_v<T, hir::ReturnStmt>) {
            if (s.value) {
                auto val_reg = lower_expr(**s.value, lir_func, current_block);
                emit(current_block, LIROpcode::Ret, {0, nullptr}, val_reg, {0, nullptr}, nullptr);
            } else {
                emit(current_block, LIROpcode::Ret, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr);
            }
        } else if constexpr (std::is_same_v<T, hir::IfStmt>) {
            auto then_label = new_label(".then");
            auto else_label = new_label(".else");
            auto end_label = new_label(".endif");

            // Push the current block (with prior statements) first
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // Evaluate condition in a fresh block
            auto cond_reg = lower_expr(*s.condition, lir_func, current_block);

            // Branch: if cond is true, jump to then
            emit(current_block, LIROpcode::Branch, {0, nullptr}, cond_reg, {0, nullptr}, nullptr, then_label);

            // Push condition block - fallthrough goes to else
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // Else block (reachable by fallthrough when condition is false)
            // Always create it - either with the else body or empty
            current_block.label = else_label;
            if (s.else_block) {
                lower_block(*s.else_block, lir_func, current_block);
            }
            emit(current_block, LIROpcode::Jump, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr, end_label);
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // Then block (reachable by Branch when condition is true)
            current_block.label = then_label;
            if (s.then_block) {
                lower_block(*s.then_block, lir_func, current_block);
            }
            emit(current_block, LIROpcode::Jump, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr, end_label);
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // End label
            current_block.label = end_label;
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};
        } else if constexpr (std::is_same_v<T, hir::WhileStmt>) {
            auto loop_label = new_label(".while");
            auto body_label = new_label(".while.body");
            auto end_label = new_label(".while.end");

            // Jump to condition check (emitted into the CURRENT block which has prior stmts)
            emit(current_block, LIROpcode::Jump, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr, loop_label);

            // Push the current block (prior stmts + jump to condition)
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // Condition block
            current_block.label = loop_label;
            auto cond_reg = lower_expr(*s.condition, lir_func, current_block);
            emit(current_block, LIROpcode::Branch, {0, nullptr}, cond_reg, {0, nullptr}, nullptr, body_label);
            emit(current_block, LIROpcode::Jump, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr, end_label);

            // Push condition block
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // Body block
            current_block.label = body_label;
            if (s.body) {
                lower_block(*s.body, lir_func, current_block);
            }
            emit(current_block, LIROpcode::Jump, {0, nullptr}, {0, nullptr}, {0, nullptr}, nullptr, loop_label);

            // Push body block
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};

            // End label (empty block - fallthrough target when condition is false)
            current_block.label = end_label;
            lir_func.blocks.push_back(std::move(current_block));
            current_block = LIRBasicBlock{};
        } else if constexpr (std::is_same_v<T, hir::Block>) {
            for (auto& inner : s.stmts) {
                lower_stmt(*inner, lir_func, current_block);
            }
        }
    }, stmt.data);
}

VirtualReg HIRToLIR::lower_expr(hir::HIRExpr& expr, LIRFunction& lir_func, LIRBasicBlock& current_block) {
    auto result = std::visit([this, &lir_func, &current_block](auto& e) -> VirtualReg {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, hir::IntLit>) {
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit(current_block, LIROpcode::Move, dest, {0, nullptr}, {0, nullptr}, e.type, "", e.value, true);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::FloatLit>) {
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit_float_move(current_block, dest, e.value, e.type);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::BoolLit>) {
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit(current_block, LIROpcode::Move, dest, {0, nullptr}, {0, nullptr}, e.type, "",
                 e.value ? 1 : 0, true);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::StringLit>) {
            VirtualReg dest{lir_func.next_reg++, e.type};
            std::string label = "__str" + std::to_string(string_counter_++);
            current_mod_->string_data.push_back({label, e.value});
            emit(current_block, LIROpcode::Load, dest, {0, nullptr}, {0, nullptr}, e.type, label);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::NullLit>) {
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit(current_block, LIROpcode::Move, dest, {0, nullptr}, {0, nullptr}, e.type, "", 0);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::VarRef>) {
            // Look up the variable's register from the map
            auto it = var_reg_map_.find(e.name);
            if (it != var_reg_map_.end()) {
                // Variable found - copy from its register
                VirtualReg dest{lir_func.next_reg++, it->second.type};
                emit(current_block, LIROpcode::Move, dest, it->second, {0, nullptr}, it->second.type);
                return dest;
            }
            // Fallback: use Load with variable name (for params, etc.)
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit(current_block, LIROpcode::Load, dest, {0, nullptr}, {0, nullptr}, e.type, e.name);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::BinaryOp>) {
            auto left = lower_expr(*e.left, lir_func, current_block);
            auto right = lower_expr(*e.right, lir_func, current_block);
            // Infer result type from operands: if either is float, result is float
            lir::TypePtr result_type = e.type;
            if (!result_type && left.type) result_type = left.type;
            if (!result_type && right.type) result_type = right.type;
            VirtualReg dest{lir_func.next_reg++, result_type};
            auto opcode = map_binary_op(e.op);
            emit(current_block, opcode, dest, left, right, result_type);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::UnaryOp>) {
            auto operand = lower_expr(*e.operand, lir_func, current_block);
            VirtualReg dest{lir_func.next_reg++, e.type};
            LIROpcode::Enum opcode;
            switch (e.op) {
                case hir::UnaryOp::Neg: opcode = LIROpcode::Sub; break;
                case hir::UnaryOp::Not: opcode = LIROpcode::LogicNot; break;
                case hir::UnaryOp::BitNot: opcode = LIROpcode::Not; break;
                default: opcode = LIROpcode::Nop; break;
            }
            emit(current_block, opcode, dest, operand, {0, nullptr}, e.type);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::CallOp>) {
            // Lower arguments
            std::vector<VirtualReg> arg_regs;
            for (auto& arg : e.args) {
                arg_regs.push_back(lower_expr(*arg, lir_func, current_block));
            }
            // Pack arguments into consecutive virtual registers starting at first_arg.id
            // so the C backend can read them sequentially
            VirtualReg first_arg{lir_func.next_reg++, nullptr};
            if (!arg_regs.empty()) {
                first_arg = arg_regs[0];
                // Ensure args are in consecutive registers from first_arg onward
                for (std::size_t i = 0; i < arg_regs.size(); ++i) {
                    lir::RegId expected = first_arg.id + static_cast<lir::RegId>(i);
                    if (arg_regs[i].id != expected) {
                        emit(current_block, LIROpcode::Move, VirtualReg{expected, arg_regs[i].type},
                             arg_regs[i], {0, nullptr}, arg_regs[i].type);
                    }
                }
            }
            VirtualReg dest{lir_func.next_reg++, e.return_type};
            emit(current_block, LIROpcode::Call, dest, first_arg, {0, nullptr}, e.return_type, e.callee_name);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::CastOp>) {
            return lower_expr(*e.value, lir_func, current_block);
        } else if constexpr (std::is_same_v<T, hir::MemberAccess>) {
            // Member access: compute struct base + offset
            // For now, treat as a variable reference using "object.member"
            VirtualReg dest{lir_func.next_reg++, e.type};
            auto obj = lower_expr(*e.object, lir_func, current_block);
            emit(current_block, LIROpcode::Load, dest, obj, {0, nullptr}, e.type, e.member);
            return dest;
        } else if constexpr (std::is_same_v<T, hir::IndexOp>) {
            auto obj = lower_expr(*e.object, lir_func, current_block);
            auto idx = lower_expr(*e.index, lir_func, current_block);
            VirtualReg dest{lir_func.next_reg++, e.type};
            emit(current_block, LIROpcode::Load, dest, obj, idx, e.type);
            return dest;
        } else {
            VirtualReg dest{lir_func.next_reg++, nullptr};
            return dest;
        }
    }, expr.data);

    return result;
}

void HIRToLIR::emit(LIRBasicBlock& block, LIROpcode::Enum op, VirtualReg dest,
                     VirtualReg src1, VirtualReg src2, TypePtr type,
                     const std::string& label, std::int64_t imm, bool has_imm) {
    LIRInstruction inst;
    inst.opcode = op;
    inst.dest = dest;
    inst.src1 = src1;
    inst.src2 = src2;
    inst.type = type;
    inst.label = label;
    inst.imm = imm;
    inst.has_imm = has_imm;
    block.instructions.push_back(std::move(inst));
}

void HIRToLIR::emit_with_var(LIRBasicBlock& block, LIROpcode::Enum op, VirtualReg dest,
                              VirtualReg src1, VirtualReg src2, TypePtr type,
                              const std::string& var_name) {
    LIRInstruction inst;
    inst.opcode = op;
    inst.dest = dest;
    inst.src1 = src1;
    inst.src2 = src2;
    inst.type = type;
    inst.var_name = var_name;
    block.instructions.push_back(std::move(inst));
}

LIROpcode::Enum HIRToLIR::map_binary_op(hir::BinaryOp::Op op) {
    switch (op) {
        case hir::BinaryOp::Add:      return LIROpcode::Add;
        case hir::BinaryOp::Sub:      return LIROpcode::Sub;
        case hir::BinaryOp::Mul:      return LIROpcode::Mul;
        case hir::BinaryOp::Div:      return LIROpcode::Div;
        case hir::BinaryOp::Mod:      return LIROpcode::Mod;
        case hir::BinaryOp::Eq:       return LIROpcode::CmpEq;
        case hir::BinaryOp::Neq:      return LIROpcode::CmpNe;
        case hir::BinaryOp::Lt:       return LIROpcode::CmpLt;
        case hir::BinaryOp::Gt:       return LIROpcode::CmpGt;
        case hir::BinaryOp::Le:       return LIROpcode::CmpLe;
        case hir::BinaryOp::Ge:       return LIROpcode::CmpGe;
        case hir::BinaryOp::BitAnd:   return LIROpcode::And;
        case hir::BinaryOp::BitOr:    return LIROpcode::Or;
        case hir::BinaryOp::BitXor:   return LIROpcode::Xor;
        case hir::BinaryOp::LShift:   return LIROpcode::Shl;
        case hir::BinaryOp::RShift:   return LIROpcode::Shr;
        case hir::BinaryOp::LogicAnd: return LIROpcode::LogicAnd;
        case hir::BinaryOp::LogicOr:  return LIROpcode::LogicOr;
    }
    return LIROpcode::Nop;
}

void HIRToLIR::emit_float_move(LIRBasicBlock& block, VirtualReg dest, double value, TypePtr type) {
    LIRInstruction inst;
    inst.opcode = LIROpcode::Move;
    inst.dest = dest;
    inst.src1 = {0, nullptr};
    inst.src2 = {0, nullptr};
    inst.type = type;
    inst.float_imm = value;
    inst.has_float_imm = true;
    block.instructions.push_back(std::move(inst));
}

} // namespace femto::lir
