#include "codegen/nasm_emitter.hpp"

namespace femto {

SemaType* NasmEmitter::resolve_type_node(ASTType* ast_ty) {
    if (!ast_ty) return nullptr;
    if (ast_ty->kind == TypeKind::Custom) {
        auto it = type_env_.find(std::string(ast_ty->custom_name));
        if (it != type_env_.end()) return it->second;
    }
    if (ast_ty->kind == TypeKind::Pointer) {
        auto* sub = resolve_type_node(ast_ty->pointee_or_element);
        return new SemaType{SemaType::Kind::Pointer, 8, 8, PointerTypeInfo{sub}};
    }
    if (ast_ty->kind == TypeKind::Array) {
        auto* elem = resolve_type_node(ast_ty->pointee_or_element);
        uint32_t sz = elem ? (uint32_t)(elem->size_bytes * ast_ty->array_size) : 0;
        return new SemaType{SemaType::Kind::Array, sz, 8, ArrayTypeInfo{elem, ast_ty->array_size}};
    }
    if (ast_ty->kind == TypeKind::Slice) {
        auto* elem = resolve_type_node(ast_ty->pointee_or_element);
        return new SemaType{SemaType::Kind::Slice, 16, 8, SliceTypeInfo{elem}};
    }
    auto it = type_env_.find("int32");
    return it != type_env_.end() ? it->second : nullptr;
}

std::string NasmEmitter::generate_assembly(const ASTProgram& program) {
    text_sec_   << "default rel\nsection .text\n";
    rodata_sec_ << "section .rodata\n";
    rodata_sec_ << "str_bounds_panic: db \"Femto panic: Slice index out of bounds\", 10, 0\n";
    data_sec_   << "section .data\n";

    for (const auto* fn : program.functions) {
        emit_function(fn);
    }

    return rodata_sec_.str() + "\n" + data_sec_.str() + "\n" + text_sec_.str();
}

void NasmEmitter::emit_function(const ASTFunctionDecl* fn) {
    local_vars_.clear();
    loop_stack_.clear();

    if (fn->is_exported || fn->name == "main") {
        text_sec_ << "global " << fn->name << "\n";
    }
    text_sec_ << fn->name << ":\n";
    text_sec_ << "    push rbp\n";
    text_sec_ << "    mov rbp, rsp\n";
    text_sec_ << "    sub rsp, 512\n";

    const char* arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    uint32_t stack_off = 0;

    for (size_t i = 0; i < fn->params.size(); ++i) {
        auto* p_ty = resolve_type_node(fn->params[i].type);
        if (!p_ty) {
            auto it = type_env_.find("int32");
            p_ty = it != type_env_.end() ? it->second : nullptr;
        }

        if (p_ty && p_ty->kind == SemaType::Kind::Slice) {
            stack_off += 16;
            VarInfo vi{ stack_off, p_ty };
            local_vars_[std::string(fn->params[i].name)] = vi;
            if (i * 2 + 1 < 6) {
                text_sec_ << "    mov [rbp - " << stack_off << "], " << arg_regs[i * 2] << "\n";
                text_sec_ << "    mov [rbp - " << (stack_off - 8) << "], " << arg_regs[i * 2 + 1] << "\n";
            }
        } else {
            stack_off += 8;
            VarInfo vi{ stack_off, p_ty };
            local_vars_[std::string(fn->params[i].name)] = vi;
            if (i < 6) {
                text_sec_ << "    mov [rbp - " << stack_off << "], " << arg_regs[i] << "\n";
            }
        }
    }

    stack_off = (stack_off + 15) & ~15;

    for (const auto* stmt : fn->body) {
        emit_statement(stmt, stack_off);
    }

    text_sec_ << "    mov rsp, rbp\n";
    text_sec_ << "    pop rbp\n";
    text_sec_ << "    ret\n\n";
}

void NasmEmitter::emit_lvalue_address(const ASTExpr* lval) {
    if (!lval) return;

    switch (lval->kind) {
        case ExprKind::Identifier: {
            auto it = local_vars_.find(std::string(lval->raw_text));
            if (it != local_vars_.end()) {
                text_sec_ << "    lea rax, [rbp - " << it->second.stack_offset << "]\n";
            }
            break;
        }
        case ExprKind::MemberAccess: {
            auto* base_expr = lval->left;
            emit_lvalue_address(base_expr);
            bool is_ptr = false;
            SemaType* st_type = nullptr;
            if (base_expr->kind == ExprKind::Identifier) {
                auto it = local_vars_.find(std::string(base_expr->raw_text));
                if (it != local_vars_.end() && it->second.type) {
                    if (it->second.type->kind == SemaType::Kind::Pointer) {
                        is_ptr = true;
                        st_type = std::get<PointerTypeInfo>(it->second.type->data).pointee;
                    } else if (it->second.type->kind == SemaType::Kind::Struct) {
                        st_type = it->second.type;
                    }
                }
            }
            if (is_ptr) {
                text_sec_ << "    mov rax, [rax]\n";
            }
            if (st_type && st_type->kind == SemaType::Kind::Struct) {
                auto& s_info = std::get<StructTypeInfo>(st_type->data);
                auto f_it = s_info.field_map.find(std::string(lval->raw_text));
                if (f_it != s_info.field_map.end()) {
                    uint32_t f_off = s_info.fields[f_it->second].offset;
                    if (f_off > 0) {
                        text_sec_ << "    add rax, " << f_off << "\n";
                    }
                }
            }
            break;
        }
        case ExprKind::Index: {
            emit_lvalue_address(lval->left);
            text_sec_ << "    push rax\n";

            bool is_slice = false;
            if (lval->left->kind == ExprKind::Identifier) {
                auto it = local_vars_.find(std::string(lval->left->raw_text));
                if (it != local_vars_.end() && it->second.type && it->second.type->kind == SemaType::Kind::Slice) {
                    is_slice = true;
                }
            }

            emit_expression(lval->right);
            text_sec_ << "    mov ebx, eax\n";
            text_sec_ << "    pop rax\n";

            if (is_slice) {
                text_sec_ << "    mov rdx, [rax]\n";
                text_sec_ << "    mov rax, rdx\n";
            }

            text_sec_ << "    movsxd rbx, ebx\n";
            text_sec_ << "    shl rbx, 2\n";
            text_sec_ << "    add rax, rbx\n";
            break;
        }
        case ExprKind::Unary: {
            if (lval->op == "*") {
                emit_expression(lval->left);
            }
            break;
        }
        default:
            break;
    }
}

void NasmEmitter::emit_statement(const ASTStmt* stmt, uint32_t& stack_offset) {
    if (!stmt) return;

    switch (stmt->kind) {
        case StmtKind::VarDecl: {
            auto* v_ty = resolve_type_node(stmt->type_annot);
            if (!v_ty) {
                auto it = type_env_.find("int32");
                v_ty = it != type_env_.end() ? it->second : nullptr;
            }

            uint32_t sz = v_ty ? v_ty->size_bytes : 4;
            sz = (sz + 7) & ~7;
            stack_offset += sz;

            VarInfo vi{ stack_offset, v_ty };
            local_vars_[std::string(stmt->name)] = vi;

            if (stmt->init_expr) {
                if (stmt->init_expr->kind == ExprKind::StructLiteral && v_ty && v_ty->kind == SemaType::Kind::Struct) {
                    auto& s_info = std::get<StructTypeInfo>(v_ty->data);
                    for (auto& sf : stmt->init_expr->struct_fields) {
                        auto f_it = s_info.field_map.find(std::string(sf.first));
                        if (f_it != s_info.field_map.end()) {
                            uint32_t f_off = s_info.fields[f_it->second].offset;
                            emit_expression(sf.second);
                            text_sec_ << "    mov [rbp - " << (stack_offset - f_off) << "], eax\n";
                        }
                    }
                } else if (stmt->init_expr->kind == ExprKind::ArrayLiteral) {
                    for (size_t i = 0; i < stmt->init_expr->args.size(); ++i) {
                        emit_expression(stmt->init_expr->args[i]);
                        text_sec_ << "    mov [rbp - " << (stack_offset - i * 4) << "], eax\n";
                    }
                } else if (stmt->init_expr->kind == ExprKind::SliceSubrange) {
                    auto* arr_expr = stmt->init_expr->left;
                    emit_lvalue_address(arr_expr);
                    text_sec_ << "    push rax\n";

                    emit_expression(stmt->init_expr->args[0]);
                    text_sec_ << "    push rax\n";

                    emit_expression(stmt->init_expr->args[1]);
                    text_sec_ << "    mov ebx, eax\n";
                    text_sec_ << "    pop rcx\n";
                    text_sec_ << "    pop rdx\n";

                    text_sec_ << "    sub ebx, ecx\n";
                    text_sec_ << "    movsxd rcx, ecx\n";
                    text_sec_ << "    shl rcx, 2\n";
                    text_sec_ << "    add rdx, rcx\n";

                    text_sec_ << "    mov [rbp - " << stack_offset << "], rdx\n";
                    text_sec_ << "    mov [rbp - " << (stack_offset - 8) << "], rbx\n";
                } else {
                    emit_expression(stmt->init_expr);
                    text_sec_ << "    mov [rbp - " << stack_offset << "], eax\n";
                }
            }
            break;
        }
        case StmtKind::Assignment: {
            emit_expression(stmt->value_expr);
            text_sec_ << "    push rax\n";
            emit_lvalue_address(stmt->target_expr);
            text_sec_ << "    pop rbx\n";
            text_sec_ << "    mov [rax], ebx\n";
            break;
        }
        case StmtKind::CompoundAssignment: {
            emit_lvalue_address(stmt->target_expr);
            text_sec_ << "    push rax\n";
            text_sec_ << "    mov eax, [rax]\n";
            text_sec_ << "    push rax\n";
            emit_expression(stmt->value_expr);
            text_sec_ << "    mov ebx, eax\n";
            text_sec_ << "    pop rax\n";

            if (stmt->op == "+=")      text_sec_ << "    add eax, ebx\n";
            else if (stmt->op == "-=") text_sec_ << "    sub eax, ebx\n";
            else if (stmt->op == "*=") text_sec_ << "    imul eax, ebx\n";
            else if (stmt->op == "/=") text_sec_ << "    cdq\n    idiv ebx\n";
            else if (stmt->op == "%=") text_sec_ << "    cdq\n    idiv ebx\n    mov eax, edx\n";

            text_sec_ << "    pop rbx\n";
            text_sec_ << "    mov [rbx], eax\n";
            break;
        }
        case StmtKind::Increment: {
            emit_lvalue_address(stmt->target_expr);
            text_sec_ << "    inc dword [rax]\n";
            break;
        }
        case StmtKind::Decrement: {
            emit_lvalue_address(stmt->target_expr);
            text_sec_ << "    dec dword [rax]\n";
            break;
        }
        case StmtKind::If: {
            std::string else_lbl = next_label(".L_else");
            std::string end_lbl  = next_label(".L_end_if");
            emit_expression(stmt->condition);
            text_sec_ << "    cmp eax, 0\n    je " << (stmt->else_block.empty() ? end_lbl : else_lbl) << "\n";
            for (auto* s : stmt->then_block) emit_statement(s, stack_offset);
            if (!stmt->else_block.empty()) {
                text_sec_ << "    jmp " << end_lbl << "\n" << else_lbl << ":\n";
                for (auto* s : stmt->else_block) emit_statement(s, stack_offset);
            }
            text_sec_ << end_lbl << ":\n";
            break;
        }
        case StmtKind::While: {
            std::string cond_lbl = next_label(".L_while_cond");
            std::string break_lbl = next_label(".L_while_break");
            loop_stack_.push_back({cond_lbl, break_lbl});
            text_sec_ << cond_lbl << ":\n";
            emit_expression(stmt->condition);
            text_sec_ << "    cmp eax, 0\n    je " << break_lbl << "\n";
            for (auto* s : stmt->then_block) emit_statement(s, stack_offset);
            text_sec_ << "    jmp " << cond_lbl << "\n" << break_lbl << ":\n";
            loop_stack_.pop_back();
            break;
        }
        case StmtKind::DoWhile: {
            std::string start_lbl = next_label(".L_do_start");
            std::string cond_lbl  = next_label(".L_do_cond");
            std::string break_lbl = next_label(".L_do_break");
            loop_stack_.push_back({cond_lbl, break_lbl});
            text_sec_ << start_lbl << ":\n";
            for (auto* s : stmt->then_block) emit_statement(s, stack_offset);
            text_sec_ << cond_lbl << ":\n";
            emit_expression(stmt->condition);
            text_sec_ << "    cmp eax, 0\n    jne " << start_lbl << "\n" << break_lbl << ":\n";
            loop_stack_.pop_back();
            break;
        }
        case StmtKind::Switch: {
            std::string break_lbl = next_label(".L_switch_break");
            loop_stack_.push_back({break_lbl, break_lbl});
            emit_expression(stmt->condition);
            text_sec_ << "    push rax\n";
            std::vector<std::string> case_labels;
            std::string default_label = break_lbl;
            for (size_t i = 0; i < stmt->switch_cases.size(); ++i) {
                std::string cl = next_label(".L_case");
                case_labels.push_back(cl);
                if (!stmt->switch_cases[i].match_val) default_label = cl;
            }
            for (size_t i = 0; i < stmt->switch_cases.size(); ++i) {
                if (stmt->switch_cases[i].match_val) {
                    emit_expression(stmt->switch_cases[i].match_val);
                    text_sec_ << "    mov ebx, eax\n    mov eax, [rsp]\n    cmp eax, ebx\n    je " << case_labels[i] << "\n";
                }
            }
            text_sec_ << "    jmp " << default_label << "\n";
            for (size_t i = 0; i < stmt->switch_cases.size(); ++i) {
                text_sec_ << case_labels[i] << ":\n";
                for (auto* s : stmt->switch_cases[i].body) emit_statement(s, stack_offset);
                text_sec_ << "    jmp " << break_lbl << "\n";
            }
            text_sec_ << break_lbl << ":\n    add rsp, 8\n";
            loop_stack_.pop_back();
            break;
        }
        case StmtKind::Break: {
            uint32_t levels = std::max(1u, stmt->loop_levels);
            if (levels <= loop_stack_.size()) {
                text_sec_ << "    jmp " << loop_stack_[loop_stack_.size() - levels].break_label << "\n";
            }
            break;
        }
        case StmtKind::Continue: {
            uint32_t levels = std::max(1u, stmt->loop_levels);
            if (levels <= loop_stack_.size()) {
                text_sec_ << "    jmp " << loop_stack_[loop_stack_.size() - levels].continue_label << "\n";
            }
            break;
        }
        case StmtKind::Return: {
            if (stmt->value_expr) emit_expression(stmt->value_expr);
            text_sec_ << "    mov rsp, rbp\n    pop rbp\n    ret\n";
            break;
        }
        case StmtKind::ExprStmt: {
            if (stmt->value_expr) emit_expression(stmt->value_expr);
            break;
        }
        default: break;
    }
}

void NasmEmitter::emit_expression(const ASTExpr* expr) {
    if (!expr) return;

    switch (expr->kind) {
        case ExprKind::Literal: 
            text_sec_ << "    mov eax, " << expr->raw_text << "\n"; break;
        case ExprKind::Identifier: {
            auto it = local_vars_.find(std::string(expr->raw_text));
            if (it != local_vars_.end()) {
                if (it->second.type && (it->second.type->kind == SemaType::Kind::Struct || it->second.type->kind == SemaType::Kind::Array)) {
                    text_sec_ << "    lea rax, [rbp - " << it->second.stack_offset << "]\n";
                } else if (it->second.type && it->second.type->kind == SemaType::Kind::Slice) {
                    text_sec_ << "    mov rax, [rbp - " << it->second.stack_offset << "]\n";
                    text_sec_ << "    mov rdx, [rbp - " << (it->second.stack_offset - 8) << "]\n";
                } else {
                    text_sec_ << "    mov eax, [rbp - " << it->second.stack_offset << "]\n";
                }
            }
            break;
        }
        case ExprKind::MemberAccess: {
            if (expr->op == "()") {
                // Method call: slice.length()
                emit_lvalue_address(expr->left);
                text_sec_ << "    mov eax, [rax + 8]\n";
            } else {
                emit_lvalue_address(expr);
                text_sec_ << "    mov eax, [rax]\n";
            }
            break;
        }
        case ExprKind::Index: {
            emit_lvalue_address(expr);
            text_sec_ << "    mov eax, [rax]\n";
            break;
        }
        case ExprKind::Unary: {
            if (expr->op == "&") {
                emit_lvalue_address(expr->left);
            } else if (expr->op == "*") {
                emit_expression(expr->left);
                text_sec_ << "    mov eax, [rax]\n";
            } else if (expr->op == "-") {
                emit_expression(expr->left);
                text_sec_ << "    neg eax\n";
            } else if (expr->op == "!") {
                emit_expression(expr->left);
                text_sec_ << "    test eax, eax\n    sete al\n    movzx eax, al\n";
            } else if (expr->op == "~") {
                emit_expression(expr->left);
                text_sec_ << "    not eax\n";
            }
            break;
        }
        case ExprKind::Binary: {
            if (expr->op == "&&") {
                std::string flbl = next_label(".L_and_f"), elbl = next_label(".L_and_e");
                emit_expression(expr->left);
                text_sec_ << "    cmp eax, 0\n    je " << flbl << "\n";
                emit_expression(expr->right);
                text_sec_ << "    cmp eax, 0\n    je " << flbl << "\n    mov eax, 1\n    jmp " << elbl << "\n" << flbl << ":\n    xor eax, eax\n" << elbl << ":\n";
                return;
            }
            if (expr->op == "||") {
                std::string tlbl = next_label(".L_or_t"), elbl = next_label(".L_or_e");
                emit_expression(expr->left);
                text_sec_ << "    cmp eax, 0\n    jne " << tlbl << "\n";
                emit_expression(expr->right);
                text_sec_ << "    cmp eax, 0\n    jne " << tlbl << "\n    xor eax, eax\n    jmp " << elbl << "\n" << tlbl << ":\n    mov eax, 1\n" << elbl << ":\n";
                return;
            }
            emit_expression(expr->left); text_sec_ << "    push rax\n";
            emit_expression(expr->right); text_sec_ << "    mov ebx, eax\n    pop rax\n";
            if (expr->op == "+") text_sec_ << "    add eax, ebx\n";
            else if (expr->op == "-") text_sec_ << "    sub eax, ebx\n";
            else if (expr->op == "*") text_sec_ << "    imul eax, ebx\n";
            else if (expr->op == "/") text_sec_ << "    cdq\n    idiv ebx\n";
            else if (expr->op == "%") text_sec_ << "    cdq\n    idiv ebx\n    mov eax, edx\n";
            else if (expr->op == "==") text_sec_ << "    cmp eax, ebx\n    sete al\n    movzx eax, al\n";
            else if (expr->op == "!=") text_sec_ << "    cmp eax, ebx\n    setne al\n    movzx eax, al\n";
            else if (expr->op == "<")  text_sec_ << "    cmp eax, ebx\n    setl al\n    movzx eax, al\n";
            else if (expr->op == "<=") text_sec_ << "    cmp eax, ebx\n    setle al\n    movzx eax, al\n";
            else if (expr->op == ">")  text_sec_ << "    cmp eax, ebx\n    setg al\n    movzx eax, al\n";
            else if (expr->op == ">=") text_sec_ << "    cmp eax, ebx\n    setge al\n    movzx eax, al\n";
            break;
        }
        case ExprKind::Call: {
            const char* arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
            size_t reg_idx = 0;
            for (size_t i = 0; i < expr->args.size() && reg_idx < 6; ++i) {
                bool is_slice = false;
                if (expr->args[i]->kind == ExprKind::Identifier) {
                    auto it = local_vars_.find(std::string(expr->args[i]->raw_text));
                    if (it != local_vars_.end() && it->second.type && it->second.type->kind == SemaType::Kind::Slice) {
                        is_slice = true;
                    }
                }
                if (is_slice) {
                    emit_expression(expr->args[i]);
                    text_sec_ << "    push rdx\n";
                    text_sec_ << "    push rax\n";
                    reg_idx += 2;
                } else {
                    emit_expression(expr->args[i]);
                    text_sec_ << "    push rax\n";
                    reg_idx += 1;
                }
            }

            for (int r = (int)reg_idx - 1; r >= 0; --r) {
                text_sec_ << "    pop " << arg_regs[r] << "\n";
            }

            if (expr->left && expr->left->kind == ExprKind::Identifier) {
                text_sec_ << "    call " << expr->left->raw_text << "\n";
            }
            break;
        }
        default: break;
    }
}

} // namespace femto