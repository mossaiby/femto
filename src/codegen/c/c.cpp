#include "c.h"

#include <algorithm>
#include <cstring>

namespace femto::codegen::c {

using TypeKind = femto::sema::TypeKind;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool CCodeGen::is_float_type(lir::TypePtr type) {
    return type && type->kind == TypeKind::Float;
}

bool CCodeGen::is_pointer_type(lir::TypePtr type) {
    if (!type) return false;
    return type->kind == TypeKind::Pointer ||
           type->kind == TypeKind::String;
}

std::string CCodeGen::mangle_name(const std::string& name) {
    std::string r = name;
    for (auto& ch : r) {
        if (ch == '.') ch = '_';
    }
    return r;
}

std::string CCodeGen::type_to_c_type(lir::TypePtr type) {
    if (!type) return "int64_t";
    switch (type->kind) {
        case TypeKind::Void:   return "void";
        case TypeKind::Bool:   return "bool";
        case TypeKind::Int:
            if (auto* t = std::get_if<femto::sema::IntType>(&type->data)) {
                if (t->bits <= 8)  return "int8_t";
                if (t->bits <= 16) return "int16_t";
                if (t->bits <= 32) return "int32_t";
                return "int64_t";
            }
            return "int64_t";
        case TypeKind::UInt:
            if (auto* t = std::get_if<femto::sema::UIntType>(&type->data)) {
                if (t->bits <= 8)  return "uint8_t";
                if (t->bits <= 16) return "uint16_t";
                if (t->bits <= 32) return "uint32_t";
                return "uint64_t";
            }
            return "uint64_t";
        case TypeKind::Float: {
            auto* t = std::get_if<femto::sema::FloatType>(&type->data);
            if (t && t->bits <= 32) return "float";
            return "double";
        }
        case TypeKind::Char:   return "uint8_t";
        case TypeKind::String: return "uint8_t*";
        case TypeKind::Pointer: return "uint8_t*";
        default:               return "int64_t";
    }
}

// All non-float values (int, pointer, bool, char, etc.) share the v[]
// array, matching the x86_64 backend's uniform-stack-slot approach.
std::string CCodeGen::var_name(lir::RegId id, lir::TypePtr type) {
    // If type is null, look up the definition type from reg_def_type_
    if (!type) {
        auto it = reg_def_type_.find(id);
        if (it != reg_def_type_.end()) type = it->second;
    }
    if (is_float_type(type))
        return "f[" + std::to_string(id) + "]";
    return "v[" + std::to_string(id) + "]";
}

int CCodeGen::count_call_args(lir::LIRInstruction& inst) {
    if (mod_) {
        for (auto& f : mod_->functions) {
            if (f.name == inst.label)
                return static_cast<int>(f.params.size());
        }
    }
    static const struct { const char* name; int count; } stubs[] = {
        {"std.io.print", 1},
        {"std.io.println", 1},
        {"__builtin_exit", 1},
        {"std.string8.from_int", 1},
        {"std.string8.from_float", 1},
        {"__femto_success", 1},
        {"__femto_failure", 1},
        {"__slice_length", 1},
    };
    for (auto& s : stubs) {
        if (inst.label == s.name) return s.count;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Runtime stubs
// ---------------------------------------------------------------------------

void CCodeGen::emit_runtime_stubs() {
    out_ << "static int32_t __femto_error_code = 0;\n\n";

    out_ << "static void std_io_print(uint8_t* msg) {\n";
    out_ << "    fputs((const char*)msg, stdout);\n";
    out_ << "}\n\n";

    out_ << "static void std_io_println(uint8_t* msg) {\n";
    out_ << "    std_io_print(msg);\n";
    out_ << "    putchar('\\n');\n";
    out_ << "}\n\n";

    out_ << "static void __builtin_exit(int32_t code) {\n";
    out_ << "    exit(code);\n";
    out_ << "}\n\n";

    out_ << "static uint8_t* std_string8_from_int(int64_t n) {\n";
    out_ << "    static char buf[32];\n";
    out_ << "    snprintf(buf, sizeof(buf), \"%ld\", (long)n);\n";
    out_ << "    return (uint8_t*)buf;\n";
    out_ << "}\n\n";

    out_ << "static uint8_t* std_string8_from_float(double f) {\n";
    out_ << "    static char buf[64];\n";
    out_ << "    snprintf(buf, sizeof(buf), \"%g\", f);\n";
    out_ << "    return (uint8_t*)buf;\n";
    out_ << "}\n\n";

    // Result type stubs
    out_ << "static int64_t __femto_success(int64_t v) {\n";
    out_ << "    __femto_error_code = 0;\n";
    out_ << "    return v;\n";
    out_ << "}\n\n";

    out_ << "static int64_t __femto_failure(int32_t c) {\n";
    out_ << "    __femto_error_code = c;\n";
    out_ << "    return 0;\n";
    out_ << "}\n\n";

    // Slice length stub
    out_ << "static int64_t __slice_length(int64_t slice_ptr) {\n";
    out_ << "    // Slice is represented as a pointer; length is passed separately\n";
    out_ << "    // For now return 0 as placeholder\n";
    out_ << "    return 0;\n";
    out_ << "}\n\n";

    // Slice element access stub
    out_ << "static int64_t* __slice_data(int64_t slice_ptr) {\n";
    out_ << "    return (int64_t*)(uintptr_t)slice_ptr;\n";
    out_ << "}\n\n";
}

// ---------------------------------------------------------------------------
// Function emission
// ---------------------------------------------------------------------------

void CCodeGen::emit_function(lir::LIRFunction& func) {
    current_func_ = &func;
    var_map_.clear();
    used_regs_.clear();
    reg_def_type_.clear();

    // Pre-scan: gather register sizes and definition types
    int max_int = 0, max_float = 0;

    // Function parameters live at registers 0..params.size()-1
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        if (is_float_type(func.params[i].second))
            max_float = std::max(max_float, (int)i);
        else
            max_int = std::max(max_int, (int)i);
    }

    for (auto& block : func.blocks) {
        for (auto& inst : block.instructions) {
            auto track = [&](lir::VirtualReg& reg) {
                if (reg.id == 0) return;
                used_regs_.insert(reg.id);
                if (is_float_type(reg.type))
                    max_float = std::max(max_float, (int)reg.id);
                else
                    max_int = std::max(max_int, (int)reg.id);
            };
            track(inst.dest);
            track(inst.src1);
            track(inst.src2);
            if (inst.dest.id != 0 && inst.dest.type &&
                inst.dest.type->kind != TypeKind::Void) {
                reg_def_type_[inst.dest.id] = inst.dest.type;
            }
            // Call instructions: try to record the return type from
            // the callee's definition; fall back to inst.dest.type.
            if (inst.opcode == lir::LIROpcode::Call && inst.dest.id != 0) {
                if (mod_) {
                    for (auto& f : mod_->functions) {
                        if (f.name == inst.label && f.return_type &&
                            f.return_type->kind != TypeKind::Void) {
                            reg_def_type_[inst.dest.id] = f.return_type;
                            break;
                        }
                    }
                }
                // Known value-returning runtime stubs
                if (inst.label == "std.string8.from_int" ||
                    inst.label == "std.string8.from_float") {
                    reg_def_type_[inst.dest.id] =
                        femto::sema::make_pointer(femto::sema::make_char(8));
                }
                // If still no type, use the LIR dest type if useful
                auto dit = reg_def_type_.find(inst.dest.id);
                if (dit == reg_def_type_.end() && inst.dest.type &&
                    inst.dest.type->kind != TypeKind::Void) {
                    reg_def_type_[inst.dest.id] = inst.dest.type;
                }
            }
        }
    }

    // Function signature
    std::string ret_type = type_to_c_type(func.return_type);
    std::string fn_name = mangle_name(func.name);

    out_ << ret_type << " " << fn_name << "(";
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        if (i > 0) out_ << ", ";
        out_ << type_to_c_type(func.params[i].second) << " p" << i;
    }
    out_ << ") {\n";

    // Register arrays (initialised to zero) - always declare to avoid undeclared identifiers
    out_ << "    int64_t v[" << std::max(1, max_int + 1) << "] = {0};\n";
    if (max_float > 0)
        out_ << "    double f[" << (max_float + 1) << "] = {0};\n";

    // Parameter → register assignments
    for (std::size_t i = 0; i < func.params.size(); ++i) {
        out_ << "    " << var_name(lir::RegId(i), func.params[i].second)
             << " = ";
        if (is_pointer_type(func.params[i].second))
            out_ << "(int64_t)(uintptr_t)";
        out_ << "p" << i << ";\n";
        if (!func.params[i].first.empty())
            var_map_[func.params[i].first] = lir::RegId(i);
    }

    if (func.blocks.empty()) {
        out_ << "}\n\n";
        return;
    }

    // Jump to first block
    out_ << "    goto " << mangle_name(
        func.blocks[0].label.empty()
            ? func.name + "_bb0"
            : func.blocks[0].label) << ";\n";

    // Basic blocks
    for (std::size_t bi = 0; bi < func.blocks.size(); ++bi) {
        auto& block = func.blocks[bi];
        std::string label = block.label.empty()
            ? func.name + "_bb" + std::to_string(bi)
            : block.label;
        out_ << mangle_name(label) << ": ;\n";

        for (auto& inst : block.instructions) {
            if (inst.opcode == lir::LIROpcode::Move && !inst.var_name.empty()) {
                var_map_[inst.var_name] = inst.dest.id;
            }
            // Track def type during emission too (in case of phi moves etc.)
            if (inst.dest.id != 0 && inst.dest.type &&
                inst.dest.type->kind != TypeKind::Void) {
                reg_def_type_[inst.dest.id] = inst.dest.type;
            }
            emit_instruction(inst);
        }
    }

    out_ << "}\n\n";
}

// ---------------------------------------------------------------------------
// Instruction emission
// ---------------------------------------------------------------------------

void CCodeGen::emit_instruction(lir::LIRInstruction& inst) {
    auto dn = [&]() -> std::string {
        return var_name(inst.dest.id, inst.dest.type);
    };
    auto s1 = [&]() -> std::string {
        return var_name(inst.src1.id, inst.src1.type);
    };
    auto s2 = [&]() -> std::string {
        return var_name(inst.src2.id, inst.src2.type);
    };
    bool dest_is_float = is_float_type(inst.dest.type);
    bool src1_is_float = is_float_type(inst.src1.type);

    switch (inst.opcode) {

    // ---- Arithmetic ----
    case lir::LIROpcode::Add:
    case lir::LIROpcode::Sub:
    case lir::LIROpcode::Mul:
    case lir::LIROpcode::Div: {
        const char* op =
            inst.opcode == lir::LIROpcode::Add ? "+" :
            inst.opcode == lir::LIROpcode::Sub ? "-" :
            inst.opcode == lir::LIROpcode::Mul ? "*" : "/";
        if (inst.opcode == lir::LIROpcode::Sub && inst.src2.id == 0) {
            out_ << "    " << dn() << " = -" << s1() << ";\n";
        } else {
            out_ << "    " << dn() << " = " << s1() << " " << op << " " << s2() << ";\n";
        }
        break;
    }

    case lir::LIROpcode::Mod:
        out_ << "    " << dn() << " = " << s1() << " % " << s2() << ";\n";
        break;

    // ---- Bitwise ----
    case lir::LIROpcode::And:
        out_ << "    " << dn() << " = " << s1() << " & " << s2() << ";\n"; break;
    case lir::LIROpcode::Or:
        out_ << "    " << dn() << " = " << s1() << " | " << s2() << ";\n"; break;
    case lir::LIROpcode::Xor:
        out_ << "    " << dn() << " = " << s1() << " ^ " << s2() << ";\n"; break;
    case lir::LIROpcode::Not:
        out_ << "    " << dn() << " = ~" << s1() << ";\n"; break;
    case lir::LIROpcode::Shl:
        out_ << "    " << dn() << " = " << s1() << " << " << s2() << ";\n"; break;
    case lir::LIROpcode::Shr:
        out_ << "    " << dn() << " = " << s1() << " >> " << s2() << ";\n"; break;

    // ---- Comparison ----
    case lir::LIROpcode::CmpEq:
    case lir::LIROpcode::CmpNe:
    case lir::LIROpcode::CmpLt:
    case lir::LIROpcode::CmpGt:
    case lir::LIROpcode::CmpLe:
    case lir::LIROpcode::CmpGe: {
        static const char* ops[] = {"==", "!=", "<", ">", "<=", ">="};
        int idx = std::clamp(inst.opcode - lir::LIROpcode::CmpEq, 0, 5);
        out_ << "    " << dn() << " = " << s1() << " " << ops[idx] << " " << s2() << ";\n";
        break;
    }

    // ---- Logical ----
    case lir::LIROpcode::LogicAnd:
        out_ << "    " << dn() << " = " << s1() << " && " << s2() << ";\n"; break;
    case lir::LIROpcode::LogicOr:
        out_ << "    " << dn() << " = " << s1() << " || " << s2() << ";\n"; break;
    case lir::LIROpcode::LogicNot:
        out_ << "    " << dn() << " = !" << s1() << ";\n"; break;

    // ---- Memory ----
    case lir::LIROpcode::Load:
        if (!inst.label.empty()) {
            auto it = var_map_.find(inst.label);
            if (it != var_map_.end()) {
                // Variable reference: load from stored register
                out_ << "    " << dn() << " = "
                     << var_name(it->second, inst.dest.type) << ";\n";
            } else if (mod_ && mod_->string_data.end() != std::find_if(mod_->string_data.begin(), mod_->string_data.end(),
                [&](const auto& sd) { return sd.first == inst.label; })) {
                // Data label (string literal address)
                out_ << "    " << dn() << " = (int64_t)(uintptr_t)"
                     << inst.label << ";\n";
            } else {
                // Unknown label - not a variable, not a string data label
                // This happens for struct member access like "bottom_right"
                if (inst.src1.id != 0) {
                    out_ << "    " << dn() << " = " << s1() << ";\n";
                } else {
                    out_ << "    " << dn() << " = 0; /* unhandled label: "
                         << inst.label << " */\n";
                }
            }
        } else if (inst.src2.id != 0) {
            // Array indexing: base[offset]
            if (dest_is_float)
                out_ << "    " << dn() << " = ((double*)"
                     << s1() << ")[" << s2() << "];\n";
            else
                out_ << "    " << dn() << " = ((int64_t*)"
                     << s1() << ")[" << s2() << "];\n";
        } else {
            // Simple pointer dereference
            if (dest_is_float)
                out_ << "    " << dn() << " = *(double*)(uintptr_t)"
                     << s1() << ";\n";
            else
                out_ << "    " << dn() << " = *(int64_t*)(uintptr_t)"
                     << s1() << ";\n";
        }
        break;

    case lir::LIROpcode::Store: {
        bool val_is_float = is_float_type(inst.src2.type);
        if (val_is_float)
            out_ << "    *(double*)(uintptr_t)" << s1()
                 << " = " << s2() << ";\n";
        else
            out_ << "    *(int64_t*)(uintptr_t)" << s1()
                 << " = " << s2() << ";\n";
        break;
    }

    case lir::LIROpcode::LoadAddr: {
        std::string target;
        if (!inst.label.empty()) {
            auto it = var_map_.find(inst.label);
            if (it != var_map_.end())
                target = var_name(it->second, inst.dest.type);
            else
                target = inst.label;
        } else {
            target = s1();
        }
        out_ << "    " << dn() << " = (int64_t)(uintptr_t)&" << target << ";\n";
        break;
    }

    // ---- Control flow ----
    case lir::LIROpcode::Jump:
        out_ << "    goto " << mangle_name(inst.label) << ";\n";
        break;

    case lir::LIROpcode::Branch:
        if (is_float_type(inst.src1.type))
            out_ << "    if (" << s1() << " != 0.0) goto "
                 << mangle_name(inst.label) << ";\n";
        else
            out_ << "    if (" << s1() << ") goto "
                 << mangle_name(inst.label) << ";\n";
        break;

    case lir::LIROpcode::Ret:
        if (inst.src1.id != 0)
            out_ << "    return " << s1() << ";\n";
        else {
            // Bare return in non-void function - return 0 as default
            std::string ret = current_func_ ? type_to_c_type(current_func_->return_type) : "void";
            if (ret == "void")
                out_ << "    return;\n";
            else
                out_ << "    return 0;\n";
        }
        break;

    // ---- Function calls ----
    case lir::LIROpcode::Call: {
        // Intercept .length() calls on slices/arrays
        bool is_length_call = inst.label.size() > 7 &&
            inst.label.substr(inst.label.size() - 7) == ".length";
        std::string callee = is_length_call ? "__slice_length" : mangle_name(inst.label);
        int num_args = is_length_call ? 1 : count_call_args(inst);

        // Determine if callee returns void — prefer known signatures over
        // the LIR instruction's dest type (which may be Void/nullptr).
        bool returns_void = true;
        if (inst.label == "std.io.print" ||
            inst.label == "std.io.println" ||
            inst.label == "__builtin_exit") {
            returns_void = true;
        } else if (inst.label == "std.string8.from_int" ||
                   inst.label == "std.string8.from_float" ||
                   inst.label == "__femto_success" ||
                   inst.label == "__femto_failure" ||
                   inst.label == "__slice_length") {
            returns_void = false;
        } else if (mod_) {
            for (auto& f : mod_->functions) {
                if (f.name == inst.label) {
                    returns_void = !f.return_type ||
                                   f.return_type->kind == TypeKind::Void;
                    break;
                }
            }
        } else if (inst.dest.type && inst.dest.type->kind != TypeKind::Void) {
            returns_void = false;
        }

        out_ << "    ";
        if (!returns_void && inst.dest.id != 0) {
            // Determine if callee returns a pointer (needs cast to int64_t)
            bool ret_is_ptr = false;
            if (inst.label == "std.string8.from_int" ||
                inst.label == "std.string8.from_float" ||
                inst.label == "__slice_length") {
                ret_is_ptr = true;
            } else if (mod_) {
                for (auto& f : mod_->functions) {
                    if (f.name == inst.label && f.return_type &&
                        (f.return_type->kind == TypeKind::Pointer ||
                         f.return_type->kind == TypeKind::String)) {
                        ret_is_ptr = true; break;
                    }
                }
            }
            if (ret_is_ptr)
                out_ << dn() << " = (int64_t)(uintptr_t)";
            else
                out_ << dn() << " = ";
        }

        out_ << callee << "(";
        for (int i = 0; i < num_args; ++i) {
            if (i > 0) out_ << ", ";
            lir::RegId aid = inst.src1.id + static_cast<lir::RegId>(i);

            // Prefer the definition type (what was stored), fall back to
            // the callee's param type, then to nullptr (→ v[] array).
            lir::TypePtr def_type = nullptr;
            auto dit = reg_def_type_.find(aid);
            if (dit != reg_def_type_.end()) def_type = dit->second;

            lir::TypePtr want_type = nullptr;
            if (mod_) {
                for (auto& f : mod_->functions) {
                    if (f.name == inst.label && (std::size_t)i < f.params.size()) {
                        want_type = f.params[i].second; break;
                    }
                }
            }
            if (!want_type) {
                // Known runtime stubs
                static const struct { const char* name; int idx; const char* type; } pt[] = {
                    {"std.io.print", 0, "uint8_t*"},
                    {"std.io.println", 0, "uint8_t*"},
                    {"__builtin_exit", 0, "int32_t"},
                    {"std.string8.from_int", 0, "int64_t"},
                    {"std.string8.from_float", 0, "double"},
                };
                for (auto& p : pt) {
                    if (inst.label == p.name && p.idx == i) {
                        out_ << "(" << p.type << ")"
                             << var_name(aid, def_type);
                        goto next_arg;
                    }
                }
            }

            if (want_type && is_pointer_type(want_type)) {
                // Callee expects a pointer - cast to pointer type
                out_ << "(uint8_t*)" << var_name(aid, def_type);
            } else if (def_type && want_type && def_type->kind != want_type->kind &&
                !is_float_type(def_type) && !is_float_type(want_type)) {
                // Int/pointer mismatch – add cast
                out_ << "(" << type_to_c_type(want_type) << ")"
                     << var_name(aid, def_type);
            } else {
                out_ << var_name(aid, def_type);
            }
        next_arg:;
        }
        out_ << ");\n";
        break;
    }

    // ---- Move ----
    case lir::LIROpcode::Move:
        if (inst.has_float_imm) {
            std::string val = std::to_string(inst.float_imm);
            if (val.find('.') == std::string::npos &&
                val.find('e') == std::string::npos &&
                val.find('E') == std::string::npos) val += ".0";
            if (is_float_type(inst.type) || dest_is_float)
                out_ << "    " << dn() << " = " << val << ";\n";
            else
                out_ << "    " << dn() << " = (int64_t)" << val << ";\n";
        } else if (!inst.label.empty()) {
            out_ << "    " << dn() << " = (int64_t)(uintptr_t)"
                 << inst.label << ";\n";
        } else if (inst.has_imm) {
            out_ << "    " << dn() << " = " << inst.imm << ";\n";
        } else if (dest_is_float && !src1_is_float)
            out_ << "    " << dn() << " = (double)" << s1() << ";\n";
        else if (!dest_is_float && src1_is_float)
            out_ << "    " << dn() << " = (int64_t)" << s1() << ";\n";
        else
            out_ << "    " << dn() << " = " << s1() << ";\n";
        break;

    case lir::LIROpcode::Cast:
        if (dest_is_float && !src1_is_float)
            out_ << "    " << dn() << " = (double)" << s1() << ";\n";
        else if (!dest_is_float && src1_is_float)
            out_ << "    " << dn() << " = (int64_t)" << s1() << ";\n";
        else
            out_ << "    " << dn() << " = " << s1() << ";\n";
        break;

    case lir::LIROpcode::Phi:
        out_ << "    " << dn() << " = " << s1() << ";\n";
        break;

    case lir::LIROpcode::Label:
        break;

    case lir::LIROpcode::Nop:
        out_ << "    ;\n";
        break;

    default:
        out_ << "    ; unhandled opcode " << inst.opcode << "\n";
        break;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

std::string CCodeGen::generate(lir::LIRModule& mod) {
    mod_ = &mod;
    out_.str("");
    out_.clear();

    out_ << "/* Generated by femto compiler (C backend) */\n\n";

    out_ << "#include <stdint.h>\n";
    out_ << "#include <stdio.h>\n";
    out_ << "#include <stdlib.h>\n";
    out_ << "#include <string.h>\n";
    out_ << "#include <stdbool.h>\n";
    out_ << "#include <math.h>\n\n";

    emit_runtime_stubs();

    // String data
    for (auto& [label, value] : mod.string_data) {
        out_ << "static const char " << label << "[] = {";
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << static_cast<int>(static_cast<unsigned char>(value[i]));
        }
        out_ << ", 0};\n";
    }
    if (!mod.string_data.empty()) out_ << "\n";

    // Forward declarations for all functions
    for (auto& func : mod.functions) {
        std::string ret_type = type_to_c_type(func.return_type);
        std::string fn_name = mangle_name(func.name);
        out_ << ret_type << " " << fn_name << "(";
        for (std::size_t i = 0; i < func.params.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << type_to_c_type(func.params[i].second) << " p" << i;
        }
        out_ << ");\n";
    }
    if (!mod.functions.empty()) out_ << "\n";

    for (auto& func : mod.functions)
        emit_function(func);

    return out_.str();
}

} // namespace femto::codegen::c
