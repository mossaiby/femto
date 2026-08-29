#include "codegen/nasm_emitter.hpp"
#include <iomanip>
#include <cctype>
#include <unordered_set>

namespace femto {

static int64_t parse_literal_int_emitter(std::string_view raw) {
    std::string s;
    for (char c : raw) {
        if (c != '_') s.push_back(c);
    }
    if (s == "true") return 1;
    if (s == "false") return 0;
    if (s.size() >= 3 && s.front() == '\'' && s.back() == '\'') {
        return (unsigned char)s[1];
    }
    if (s.rfind("0b", 0) == 0 || s.rfind("0B", 0) == 0) {
        return std::stoll(s.substr(2), nullptr, 2);
    }
    return std::stoll(s, nullptr, 0);
}

static std::string sanitize_symbol_raw(std::string_view name) {
    std::string s;
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == ':' && i + 1 < name.size() && name[i + 1] == ':') {
            s.push_back('_');
            i++;
        } else if (name[i] == ':' || name[i] == '.' || name[i] == '/' || name[i] == '-') {
            s.push_back('_');
        } else {
            s.push_back(name[i]);
        }
    }
    return s;
}

static std::string sanitize_nasm_identifier(std::string_view name) {
    std::string s = sanitize_symbol_raw(name);

    static const std::unordered_set<std::string> nasm_keywords = {
        "abs", "fabs", "rel", "seg", "wrt", "strict", "default", "nosplit",
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
        "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
        "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh",
        "byte", "word", "dword", "qword", "tword", "oword", "yword", "zword"
    };

    if (nasm_keywords.count(s)) {
        return "$" + s;
    }
    return s;
}

bool NasmEmitter::is_float_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Literal) {
        if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`' || expr->raw_text.front() == '\'') {
            return false;
        }
        return (expr->raw_text.find('.') != std::string_view::npos ||
                expr->raw_text.find('e') != std::string_view::npos ||
                expr->raw_text.find('E') != std::string_view::npos);
    }
    if (expr->kind == ExprKind::Identifier) {
        auto it = local_vars_.find(std::string(expr->raw_text));
        if (it != local_vars_.end() && it->second.type && it->second.type->is_floating_point()) {
            return true;
        }
        return false;
    }
    if (expr->kind == ExprKind::Binary) {
        if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "/") {
            return is_float_expr(expr->left) || is_float_expr(expr->right);
        }
        return false;
    }
    if (expr->kind == ExprKind::Call) {
        if (current_program_ && expr->left && expr->left->kind == ExprKind::Identifier) {
            std::string callee = std::string(expr->left->raw_text);
            for (const auto* fn : current_program_->functions) {
                if (fn->name == callee || sanitize_symbol_raw(fn->name) == sanitize_symbol_raw(callee)) {
                    auto* ret_ty = resolve_type_node(fn->return_type);
                    if (ret_ty && ret_ty->is_floating_point()) return true;
                }
            }
        }
    }
    return false;
}

int64_t NasmEmitter::eval_const_expr(const ASTExpr* expr) {
    if (!expr) return 0;
    if (expr->kind == ExprKind::Literal) {
        return parse_literal_int_emitter(expr->raw_text);
    }
    if (expr->kind == ExprKind::Identifier) {
        auto it = const_defs_.find(std::string(expr->raw_text));
        if (it != const_defs_.end()) return it->second;
    }
    if (expr->kind == ExprKind::BuiltinSizeof && expr->target_type) {
        auto* ty = resolve_type_node(expr->target_type);
        return ty ? ty->size_bytes : 4;
    }
    if (expr->kind == ExprKind::BuiltinAlignof && expr->target_type) {
        auto* ty = resolve_type_node(expr->target_type);
        return ty ? ty->align_bytes : 4;
    }
    if (expr->kind == ExprKind::Binary) {
        int64_t l = eval_const_expr(expr->left);
        int64_t r = eval_const_expr(expr->right);
        if (expr->op == "+") return l + r;
        if (expr->op == "-") return l - r;
        if (expr->op == "*") return l * r;
        if (expr->op == "/") return r != 0 ? l / r : 0;
        if (expr->op == "==") return l == r;
        if (expr->op == "!=") return l != r;
        if (expr->op == "<") return l < r;
        if (expr->op == "<=") return l <= r;
        if (expr->op == ">") return l > r;
        if (expr->op == ">=") return l >= r;
    }
    return 0;
}

SemaType* NasmEmitter::resolve_type_node(ASTType* ast_ty) {
    if (!ast_ty) return nullptr;
    if (ast_ty->kind == TypeKind::Primitive) {
        for (auto& [name, ty] : type_env_) {
            if (ty->kind == SemaType::Kind::Primitive && 
                std::get<PrimitiveTypeInfo>(ty->data).kind == ast_ty->primitive_kind) {
                return ty;
            }
        }
    }
    if (ast_ty->kind == TypeKind::Custom) {
        std::string name = std::string(ast_ty->custom_name);
        if (!ast_ty->generic_args.empty()) {
            for (auto* a : ast_ty->generic_args) {
                name += "__" + TypeChecker::get_type_name(a);
            }
        }
        auto it = type_env_.find(name);
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
    if (ast_ty->kind == TypeKind::Result) {
        auto* payload = resolve_type_node(ast_ty->pointee_or_element);
        return new SemaType{SemaType::Kind::Result, 16, 8, ResultTypeInfo{payload}};
    }
    auto it = type_env_.find("int32");
    return it != type_env_.end() ? it->second : nullptr;
}

std::string NasmEmitter::generate_assembly(const ASTProgram& program) {
    current_program_ = &program;
    text_sec_   << "default rel\nsection .text\n";
    rodata_sec_ << "section .rodata\n";
    rodata_sec_ << "str_bounds_panic: db \"Femto panic: Slice index out of bounds\", 10, 0\n";
    data_sec_   << "section .data\n";

    std::unordered_set<std::string> extern_declared;
    for (const auto* fn : program.functions) {
        if (fn->is_extern_c) {
            std::string ext_name = std::string(fn->name);
            auto last_colons = ext_name.rfind("::");
            if (last_colons != std::string::npos) {
                ext_name = ext_name.substr(last_colons + 2);
            }
            ext_name = sanitize_symbol_raw(ext_name);
            if (!extern_declared.count(ext_name)) {
                extern_declared.insert(ext_name);
                text_sec_ << "extern " << ext_name << "\n";
            }
        }
    }

    for (const auto* fn : program.functions) {
        if (fn->generic_params.empty() && !fn->is_extern_c) {
            emit_function(fn);
        }
    }

    return rodata_sec_.str() + "\n" + data_sec_.str() + "\n" + text_sec_.str();
}

void NasmEmitter::emit_function(const ASTFunctionDecl* fn) {
    local_vars_.clear();
    loop_stack_.clear();
    subject_stack_.clear();

    std::string fn_lbl = sanitize_symbol_raw(fn->name);

    if (fn->is_exported || fn->name == "main") {
        text_sec_ << "global " << fn_lbl << "\n";
    }
    text_sec_ << fn_lbl << ":\n";
    text_sec_ << "    push rbp\n";
    text_sec_ << "    mov rbp, rsp\n";
    text_sec_ << "    sub rsp, 512\n";

    const char* int_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    size_t int_idx = 0;
    size_t flt_idx = 0;
    uint32_t stack_off = 0;

    for (size_t i = 0; i < fn->params.size(); ++i) {
        auto* p_ty = resolve_type_node(fn->params[i].type);
        if (!p_ty) {
            auto it = type_env_.find("int32");
            p_ty = it != type_env_.end() ? it->second : nullptr;
        }

        if (p_ty && p_ty->is_floating_point()) {
            stack_off += 8;
            VarInfo vi{ stack_off, p_ty };
            local_vars_[std::string(fn->params[i].name)] = vi;
            if (flt_idx < 8) {
                if (p_ty->size_bytes == 4) {
                    text_sec_ << "    movss [rbp - " << stack_off << "], xmm" << flt_idx++ << "\n";
                } else {
                    text_sec_ << "    movsd [rbp - " << stack_off << "], xmm" << flt_idx++ << "\n";
                }
            }
        } else if (p_ty && p_ty->kind == SemaType::Kind::Slice) {
            stack_off += 16;
            VarInfo vi{ stack_off, p_ty };
            local_vars_[std::string(fn->params[i].name)] = vi;
            if (int_idx * 2 + 1 < 6) {
                text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs[int_idx * 2] << "\n";
                text_sec_ << "    mov [rbp - " << (stack_off - 8) << "], " << int_arg_regs[int_idx * 2 + 1] << "\n";
            }
        } else {
            stack_off += 8;
            VarInfo vi{ stack_off, p_ty };
            local_vars_[std::string(fn->params[i].name)] = vi;
            if (int_idx < 6) {
                text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs[int_idx++] << "\n";
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
                            if (s_info.fields[f_it->second].type && s_info.fields[f_it->second].type->size_bytes == 8) {
                                text_sec_ << "    mov [rbp - " << (stack_offset - f_off) << "], rax\n";
                            } else {
                                text_sec_ << "    mov [rbp - " << (stack_offset - f_off) << "], eax\n";
                            }
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
                } else if (v_ty && v_ty->kind == SemaType::Kind::Struct) {
                    emit_expression(stmt->init_expr);
                    text_sec_ << "    mov rsi, rax\n";
                    text_sec_ << "    lea rdi, [rbp - " << stack_offset << "]\n";
                    for (uint32_t b = 0; b < v_ty->size_bytes; b += 8) {
                        text_sec_ << "    mov r8, [rsi + " << b << "]\n";
                        text_sec_ << "    mov [rdi + " << b << "], r8\n";
                    }
                } else if (v_ty && v_ty->is_floating_point()) {
                    emit_expression(stmt->init_expr);
                    if (v_ty->size_bytes == 4) {
                        text_sec_ << "    movss [rbp - " << stack_offset << "], xmm0\n";
                    } else {
                        text_sec_ << "    movsd [rbp - " << stack_offset << "], xmm0\n";
                    }
                } else {
                    emit_expression(stmt->init_expr);
                    if (v_ty && (v_ty->kind == SemaType::Kind::Pointer || v_ty->size_bytes == 8)) {
                        text_sec_ << "    mov [rbp - " << stack_offset << "], rax\n";
                    } else {
                        text_sec_ << "    mov [rbp - " << stack_offset << "], eax\n";
                    }
                }
            }
            break;
        }
        case StmtKind::HashIf: {
            int64_t cond_val = eval_const_expr(stmt->condition);
            if (cond_val != 0) {
                for (auto* s : stmt->then_block) emit_statement(s, stack_offset);
            } else {
                for (auto* s : stmt->else_block) emit_statement(s, stack_offset);
            }
            break;
        }
        case StmtKind::Assignment: {
            if (is_float_expr(stmt->target_expr) || is_float_expr(stmt->value_expr)) {
                emit_expression(stmt->value_expr);
                text_sec_ << "    sub rsp, 8\n    movsd [rsp], xmm0\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    movsd xmm0, [rsp]\n    add rsp, 8\n";
                text_sec_ << "    movsd [rax], xmm0\n";
            } else {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n";
                text_sec_ << "    mov [rax], rbx\n";
            }
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
            std::string else_lbl = next_label("L_else");
            std::string end_lbl  = next_label("L_end_if");
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
            std::string cond_lbl = next_label("L_while_cond");
            std::string break_lbl = next_label("L_while_break");
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
            std::string start_lbl = next_label("L_do_start");
            std::string cond_lbl  = next_label("L_do_cond");
            std::string break_lbl = next_label("L_do_break");
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
            std::string break_lbl = next_label("L_switch_break");
            loop_stack_.push_back({break_lbl, break_lbl});
            emit_expression(stmt->condition);
            text_sec_ << "    push rax\n";
            std::vector<std::string> case_labels;
            std::string default_label = break_lbl;
            for (size_t i = 0; i < stmt->switch_cases.size(); ++i) {
                std::string cl = next_label("L_case");
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
        case StmtKind::Foreach: {
            std::string cond_lbl  = next_label("L_foreach_cond");
            std::string break_lbl = next_label("L_foreach_break");
            loop_stack_.push_back({cond_lbl, break_lbl});

            emit_lvalue_address(stmt->iter_collection);
            
            stack_offset += 8;
            uint32_t coll_off = stack_offset;
            text_sec_ << "    mov [rbp - " << coll_off << "], rax\n";

            bool is_slice = false;
            uint32_t arr_len = 0;
            if (stmt->iter_collection->kind == ExprKind::Identifier) {
                auto it = local_vars_.find(std::string(stmt->iter_collection->raw_text));
                if (it != local_vars_.end() && it->second.type) {
                    if (it->second.type->kind == SemaType::Kind::Slice) {
                        is_slice = true;
                    } else if (it->second.type->kind == SemaType::Kind::Array) {
                        arr_len = std::get<ArrayTypeInfo>(it->second.type->data).size;
                    }
                }
            }

            stack_offset += 8;
            uint32_t iter_var_off = stack_offset;
            local_vars_[std::string(stmt->iter_var)] = VarInfo{ iter_var_off, type_env_.find("int32")->second };

            uint32_t iter_idx_off = 0;
            if (!stmt->iter_idx.empty()) {
                stack_offset += 8;
                iter_idx_off = stack_offset;
                local_vars_[std::string(stmt->iter_idx)] = VarInfo{ iter_idx_off, type_env_.find("int32")->second };
            }

            stack_offset += 8;
            uint32_t loop_i_off = stack_offset;
            text_sec_ << "    mov dword [rbp - " << loop_i_off << "], 0\n";

            text_sec_ << cond_lbl << ":\n";
            text_sec_ << "    mov eax, [rbp - " << loop_i_off << "]\n";
            if (is_slice) {
                text_sec_ << "    mov rbx, [rbp - " << coll_off << "]\n";
                text_sec_ << "    mov ecx, [rbx + 8]\n";
                text_sec_ << "    cmp eax, ecx\n";
            } else {
                text_sec_ << "    cmp eax, " << arr_len << "\n";
            }
            text_sec_ << "    jge " << break_lbl << "\n";

            text_sec_ << "    mov rbx, [rbp - " << coll_off << "]\n";
            if (is_slice) {
                text_sec_ << "    mov rbx, [rbx]\n";
            }
            text_sec_ << "    movsxd rax, eax\n";
            text_sec_ << "    shl rax, 2\n";
            text_sec_ << "    add rbx, rax\n";
            text_sec_ << "    mov eax, [rbx]\n";
            text_sec_ << "    mov [rbp - " << iter_var_off << "], eax\n";

            if (!stmt->iter_idx.empty()) {
                text_sec_ << "    mov eax, [rbp - " << loop_i_off << "]\n";
                text_sec_ << "    mov [rbp - " << iter_idx_off << "], eax\n";
            }

            for (auto* s : stmt->then_block) emit_statement(s, stack_offset);

            text_sec_ << "    inc dword [rbp - " << loop_i_off << "]\n";
            text_sec_ << "    jmp " << cond_lbl << "\n";

            text_sec_ << break_lbl << ":\n";
            loop_stack_.pop_back();
            break;
        }
        case StmtKind::ResultBranch: {
            std::string fail_lbl = next_label("L_res_fail");
            std::string end_lbl  = next_label("L_res_end");

            emit_expression(stmt->condition);
            text_sec_ << "    test rdx, rdx\n";
            text_sec_ << "    jnz " << fail_lbl << "\n";

            stack_offset += 8;
            local_vars_[std::string(stmt->success_var)] = VarInfo{ stack_offset, type_env_.find("int32")->second };
            text_sec_ << "    mov [rbp - " << stack_offset << "], eax\n";
            for (auto* s : stmt->success_block) emit_statement(s, stack_offset);
            text_sec_ << "    jmp " << end_lbl << "\n";

            text_sec_ << fail_lbl << ":\n";
            stack_offset += 8;
            local_vars_[std::string(stmt->failure_var)] = VarInfo{ stack_offset, type_env_.find("int32")->second };
            text_sec_ << "    mov [rbp - " << stack_offset << "], edx\n";
            for (auto* s : stmt->failure_block) emit_statement(s, stack_offset);

            text_sec_ << end_lbl << ":\n";
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
        case ExprKind::Literal: {
            if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`') {
                std::string str_lbl = next_label("L_str");
                std::string raw = std::string(expr->raw_text.substr(1, expr->raw_text.size() - 2));
                rodata_sec_ << str_lbl << ": db ";
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (raw[i] == '\\' && i + 1 < raw.size()) {
                        i++;
                        if (raw[i] == 'n') rodata_sec_ << "10, ";
                        else if (raw[i] == 't') rodata_sec_ << "9, ";
                        else if (raw[i] == '0') rodata_sec_ << "0, ";
                        else rodata_sec_ << (int)(unsigned char)raw[i] << ", ";
                    } else {
                        rodata_sec_ << (int)(unsigned char)raw[i] << ", ";
                    }
                }
                rodata_sec_ << "0\n";
                text_sec_ << "    lea rax, [rel " << str_lbl << "]\n";
            } else if (is_float_expr(expr)) {
                std::string flt_lbl = next_label("L_f64");
                std::string clean;
                for (char c : expr->raw_text) if (c != '_') clean.push_back(c);
                rodata_sec_ << flt_lbl << ": dq " << clean << "\n";
                text_sec_ << "    movsd xmm0, [rel " << flt_lbl << "]\n";
            } else if (expr->raw_text == "true") {
                text_sec_ << "    mov eax, 1\n";
            } else if (expr->raw_text == "false") {
                text_sec_ << "    xor eax, eax\n";
            } else if (expr->raw_text.front() == '\'') {
                int64_t c_val = parse_literal_int_emitter(expr->raw_text);
                text_sec_ << "    mov eax, " << c_val << "\n";
            } else {
                int64_t val = parse_literal_int_emitter(expr->raw_text);
                text_sec_ << "    mov rax, " << val << "\n";
            }
            break;
        }
        case ExprKind::Subject: {
            if (!subject_stack_.empty()) {
                text_sec_ << "    mov eax, [rbp - " << subject_stack_.back() << "]\n";
            }
            break;
        }
        case ExprKind::BuiltinSizeof: {
            auto* ty = resolve_type_node(expr->target_type);
            uint32_t sz = ty ? ty->size_bytes : 4;
            text_sec_ << "    mov eax, " << sz << "\n";
            break;
        }
        case ExprKind::BuiltinAlignof: {
            auto* ty = resolve_type_node(expr->target_type);
            uint32_t al = ty ? ty->align_bytes : 4;
            text_sec_ << "    mov eax, " << al << "\n";
            break;
        }
        case ExprKind::BuiltinBitcast: {
            emit_expression(expr->left);
            break;
        }
        case ExprKind::Identifier: {
            auto c_it = const_defs_.find(std::string(expr->raw_text));
            if (c_it != const_defs_.end()) {
                text_sec_ << "    mov rax, " << c_it->second << "\n";
                return;
            }

            auto sep = expr->raw_text.find("::");
            if (sep != std::string_view::npos) {
                std::string enum_name(expr->raw_text.substr(0, sep));
                std::string var_name(expr->raw_text.substr(sep + 2));
                auto e_it = enum_defs_.find(enum_name);
                if (e_it != enum_defs_.end()) {
                    auto v_it = e_it->second.find(var_name);
                    if (v_it != e_it->second.end()) {
                        text_sec_ << "    mov eax, " << v_it->second << "\n";
                        return;
                    }
                }
            }

            auto it = local_vars_.find(std::string(expr->raw_text));
            if (it != local_vars_.end()) {
                if (it->second.type && it->second.type->is_floating_point()) {
                    if (it->second.type->size_bytes == 4) {
                        text_sec_ << "    movss xmm0, [rbp - " << it->second.stack_offset << "]\n";
                    } else {
                        text_sec_ << "    movsd xmm0, [rbp - " << it->second.stack_offset << "]\n";
                    }
                } else if (it->second.type && (it->second.type->kind == SemaType::Kind::Struct || it->second.type->kind == SemaType::Kind::Array)) {
                    text_sec_ << "    lea rax, [rbp - " << it->second.stack_offset << "]\n";
                } else if (it->second.type && (it->second.type->kind == SemaType::Kind::Pointer || it->second.type->size_bytes == 8)) {
                    text_sec_ << "    mov rax, [rbp - " << it->second.stack_offset << "]\n";
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
                emit_lvalue_address(expr->left);
                text_sec_ << "    mov eax, [rax + 8]\n";
            } else {
                emit_lvalue_address(expr);
                text_sec_ << "    mov rax, [rax]\n";
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
                text_sec_ << "    mov rax, [rax]\n";
            } else if (expr->op == "success") {
                if (expr->left) {
                    emit_expression(expr->left);
                } else {
                    text_sec_ << "    xor eax, eax\n";
                }
                text_sec_ << "    xor edx, edx\n";
            } else if (expr->op == "failure") {
                if (expr->left) {
                    emit_expression(expr->left);
                    text_sec_ << "    mov edx, eax\n";
                } else {
                    text_sec_ << "    mov edx, 1\n";
                }
                text_sec_ << "    xor eax, eax\n";
            } else if (expr->op == "-") {
                if (is_float_expr(expr->left)) {
                    emit_expression(expr->left);
                    text_sec_ << "    xorpd xmm1, xmm1\n    subsd xmm1, xmm0\n    movsd xmm0, xmm1\n";
                } else {
                    emit_expression(expr->left);
                    text_sec_ << "    neg eax\n";
                }
            } else if (expr->op == "!") {
                emit_expression(expr->left);
                text_sec_ << "    test eax, eax\n    sete al\n    movzx eax, al\n";
            } else if (expr->op == "~") {
                emit_expression(expr->left);
                text_sec_ << "    not eax\n";
            }
            break;
        }
        case ExprKind::PostfixUnwrap: {
            std::string ok_lbl = next_label("L_unwrap_ok");
            emit_expression(expr->left);
            text_sec_ << "    test rdx, rdx\n";
            text_sec_ << "    jz " << ok_lbl << "\n";
            text_sec_ << "    mov rsp, rbp\n    pop rbp\n    ret\n";
            text_sec_ << ok_lbl << ":\n";
            break;
        }
        case ExprKind::Match: {
            std::string match_end_lbl = next_label("L_match_end");
            emit_expression(expr->left);

            uint32_t subj_off = 480;
            subject_stack_.push_back(subj_off);
            text_sec_ << "    mov [rbp - " << subj_off << "], eax\n";

            for (size_t i = 0; i < expr->match_arms.size(); ++i) {
                std::string next_arm_lbl = next_label("L_match_next");
                if (expr->match_arms[i].condition) {
                    emit_expression(expr->match_arms[i].condition);
                    text_sec_ << "    test eax, eax\n";
                    text_sec_ << "    jz " << next_arm_lbl << "\n";
                }

                if (expr->match_arms[i].result_expr) {
                    emit_expression(expr->match_arms[i].result_expr);
                }
                text_sec_ << "    jmp " << match_end_lbl << "\n";
                text_sec_ << next_arm_lbl << ":\n";
            }

            text_sec_ << match_end_lbl << ":\n";
            subject_stack_.pop_back();
            break;
        }
        case ExprKind::Binary: {
            if (expr->op == "&&") {
                std::string flbl = next_label("L_and_f"), elbl = next_label("L_and_e");
                emit_expression(expr->left);
                text_sec_ << "    cmp eax, 0\n    je " << flbl << "\n";
                emit_expression(expr->right);
                text_sec_ << "    cmp eax, 0\n    je " << flbl << "\n    mov eax, 1\n    jmp " << elbl << "\n" << flbl << ":\n    xor eax, eax\n" << elbl << ":\n";
                return;
            }
            if (expr->op == "||") {
                std::string tlbl = next_label("L_or_t"), elbl = next_label("L_or_e");
                emit_expression(expr->left);
                text_sec_ << "    cmp eax, 0\n    jne " << tlbl << "\n";
                emit_expression(expr->right);
                text_sec_ << "    cmp eax, 0\n    jne " << tlbl << "\n    xor eax, eax\n    jmp " << elbl << "\n" << tlbl << ":\n    mov eax, 1\n" << elbl << ":\n";
                return;
            }

            if (is_float_expr(expr->left) || is_float_expr(expr->right)) {
                emit_expression(expr->left);
                text_sec_ << "    sub rsp, 8\n    movsd [rsp], xmm0\n";
                emit_expression(expr->right);
                text_sec_ << "    movsd xmm1, xmm0\n";
                text_sec_ << "    movsd xmm0, [rsp]\n    add rsp, 8\n";

                if (expr->op == "+")      text_sec_ << "    addsd xmm0, xmm1\n";
                else if (expr->op == "-") text_sec_ << "    subsd xmm0, xmm1\n";
                else if (expr->op == "*") text_sec_ << "    mulsd xmm0, xmm1\n";
                else if (expr->op == "/") text_sec_ << "    divsd xmm0, xmm1\n";
                else if (expr->op == "==") text_sec_ << "    ucomisd xmm0, xmm1\n    sete al\n    setnp bl\n    and al, bl\n    movzx eax, al\n";
                else if (expr->op == "!=") text_sec_ << "    ucomisd xmm0, xmm1\n    setne al\n    setp bl\n    or al, bl\n    movzx eax, al\n";
                else if (expr->op == "<")  text_sec_ << "    ucomisd xmm0, xmm1\n    setb al\n    movzx eax, al\n";
                else if (expr->op == "<=") text_sec_ << "    ucomisd xmm0, xmm1\n    setbe al\n    movzx eax, al\n";
                else if (expr->op == ">")  text_sec_ << "    ucomisd xmm0, xmm1\n    seta al\n    movzx eax, al\n";
                else if (expr->op == ">=") text_sec_ << "    ucomisd xmm0, xmm1\n    setae al\n    movzx eax, al\n";
                return;
            }

            emit_expression(expr->left);  text_sec_ << "    push rax\n";
            emit_expression(expr->right); text_sec_ << "    mov rbx, rax\n    pop rax\n";
            if (expr->op == "+") text_sec_ << "    add rax, rbx\n";
            else if (expr->op == "-") text_sec_ << "    sub rax, rbx\n";
            else if (expr->op == "*") text_sec_ << "    imul rax, rbx\n";
            else if (expr->op == "/") text_sec_ << "    cdq\n    idiv ebx\n";
            else if (expr->op == "%") text_sec_ << "    cdq\n    idiv ebx\n    mov eax, edx\n";
            else if (expr->op == "==") text_sec_ << "    cmp rax, rbx\n    sete al\n    movzx eax, al\n";
            else if (expr->op == "!=") text_sec_ << "    cmp rax, rbx\n    setne al\n    movzx eax, al\n";
            else if (expr->op == "<")  text_sec_ << "    cmp rax, rbx\n    setl al\n    movzx eax, al\n";
            else if (expr->op == "<=") text_sec_ << "    cmp rax, rbx\n    setle al\n    movzx eax, al\n";
            else if (expr->op == ">")  text_sec_ << "    cmp rax, rbx\n    setg al\n    movzx eax, al\n";
            else if (expr->op == ">=") text_sec_ << "    cmp rax, rbx\n    setge al\n    movzx eax, al\n";
            break;
        }
        case ExprKind::Call: {
            const char* int_arg_regs[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
            
            // Analyze each arg type to preserve calling convention
            std::vector<bool> is_float_arg;
            for (size_t i = 0; i < expr->args.size(); ++i) {
                is_float_arg.push_back(is_float_expr(expr->args[i]));
            }

            for (size_t i = 0; i < expr->args.size(); ++i) {
                if (is_float_arg[i]) {
                    emit_expression(expr->args[i]);
                    text_sec_ << "    sub rsp, 8\n    movsd [rsp], xmm0\n";
                } else {
                    emit_expression(expr->args[i]);
                    text_sec_ << "    push rax\n";
                }
            }

            // Pop in reverse order to correct registers
            int int_count = 0;
            int flt_count = 0;
            for (size_t i = 0; i < expr->args.size(); ++i) {
                if (is_float_arg[i]) flt_count++;
                else int_count++;
            }

            int cur_flt = flt_count - 1;
            int cur_int = int_count - 1;

            for (int i = (int)expr->args.size() - 1; i >= 0; --i) {
                if (is_float_arg[i]) {
                    if (cur_flt < 8) {
                        text_sec_ << "    movsd xmm" << cur_flt << ", [rsp]\n    add rsp, 8\n";
                    } else {
                        text_sec_ << "    add rsp, 8\n";
                    }
                    cur_flt--;
                } else {
                    if (cur_int < 6) {
                        text_sec_ << "    pop " << int_arg_regs[cur_int] << "\n";
                    } else {
                        text_sec_ << "    add rsp, 8\n";
                    }
                    cur_int--;
                }
            }

            if (flt_count > 0) {
                text_sec_ << "    mov al, " << std::min(flt_count, 8) << "\n";
            }

            if (expr->left && expr->left->kind == ExprKind::Identifier) {
                std::string callee = std::string(expr->left->raw_text);
                if (!expr->generic_args.empty() && callee.find("__") == std::string::npos) {
                    for (auto* a : expr->generic_args) {
                        callee += "__" + TypeChecker::get_type_name(a);
                    }
                }

                if (current_program_) {
                    for (const auto* fn : current_program_->functions) {
                        if (fn->is_extern_c) {
                            std::string fn_n = std::string(fn->name);
                            if (callee == fn_n || callee.ends_with("::" + fn_n)) {
                                auto last_colons = fn_n.rfind("::");
                                callee = (last_colons != std::string::npos) ? fn_n.substr(last_colons + 2) : fn_n;
                                break;
                            }
                        }
                    }
                }

                text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
            }
            break;
        }
        default: break;
    }
}

} // namespace femto