#include "codegen/nasm_emitter.hpp"
#include <iomanip>
#include <cctype>
#include <functional>
#include <unordered_set>
#include <algorithm>

namespace femto {

static int64_t parse_literal_int_emitter(std::string_view raw) {
    std::string s;
    for (char c : raw) {
        if (c != '_') s.push_back(c);
    }
    if (s == "true") return 1;
    if (s == "false") return 0;
    if (s == "null") return 0;
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

bool NasmEmitter::is_string_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Literal) {
        return expr->raw_text.front() == '"' || expr->raw_text.front() == '`';
    }
    if (expr->kind == ExprKind::BuiltinFile || expr->kind == ExprKind::BuiltinTarget ||
        expr->kind == ExprKind::BuiltinArch || expr->kind == ExprKind::BuiltinEndian ||
        expr->kind == ExprKind::BuiltinTypeof) {
        return true;
    }
    auto* ty = get_expr_type(expr);
    return ty && (ty->is_primitive(TokenKind::KwString8) || ty->is_primitive(TokenKind::KwString16) || ty->is_primitive(TokenKind::KwString32));
}

bool NasmEmitter::is_16byte_type(const SemaType* ty) {
    if (!ty) return false;
    if (ty->is_128bit()) return true;
    if (ty->kind == SemaType::Kind::Slice) return true;
    if (ty->kind == SemaType::Kind::Result) return true;
    if (ty->is_primitive(TokenKind::KwAny)) return true;
    if ((ty->kind == SemaType::Kind::Struct || ty->kind == SemaType::Kind::Union) && ty->size_bytes == 16) return true;
    return false;
}

bool NasmEmitter::is_16byte_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (is_128bit_expr(expr) || is_slice_expr(expr)) return true;
    auto* ty = get_expr_type(expr);
    return is_16byte_type(ty);
}

uint64_t NasmEmitter::get_type_id(const ASTExpr* expr) {
    if (!expr) return 4;
    if (expr->kind == ExprKind::Literal) {
        if (expr->raw_text == "true" || expr->raw_text == "false") return 13; // TYPE_BOOL
        if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`') return 17; // TYPE_STRING
        if (expr->raw_text.front() == '\'') return 14; // TYPE_CHAR
        if (expr->raw_text == "null") return 18; // TYPE_POINTER
        if (is_float_expr(expr)) return 12; // TYPE_FLOAT64
    }
    if (is_string_expr(expr)) return 17; // TYPE_STRING
    if (is_float_expr(expr)) return 12; // TYPE_FLOAT64
    if (is_128bit_expr(expr)) return 5; // TYPE_INT128
    auto* ty = get_expr_type(expr);
    if (ty) {
        if (ty->kind == SemaType::Kind::Pointer) return 18;
        if (ty->is_primitive(TokenKind::KwString8) || ty->is_primitive(TokenKind::KwString16) || ty->is_primitive(TokenKind::KwString32)) return 17;
        if (ty->is_primitive(TokenKind::KwBool8) || ty->is_primitive(TokenKind::KwBool16) || ty->is_primitive(TokenKind::KwBool32) || ty->is_primitive(TokenKind::KwBool64)) return 13;
        if (ty->is_primitive(TokenKind::KwChar8) || ty->is_primitive(TokenKind::KwChar16) || ty->is_primitive(TokenKind::KwChar32)) return 14;
        if (ty->is_primitive(TokenKind::KwFloat32)) return 11;
        if (ty->is_primitive(TokenKind::KwFloat64)) return 12;
        if (ty->is_integer()) return 4;
    }
    return 4; // default INT64
}

uint32_t NasmEmitter::calculate_function_stack_size(const ASTFunctionDecl* fn) {
    uint32_t current_offset = 0;

    for (const auto& param : fn->params) {
        auto* p_ty = resolve_type_node(param.type);
        if (!p_ty) {
            auto it = type_env_.find("int32");
            p_ty = it != type_env_.end() ? it->second : nullptr;
        }
        if (param.is_variadic_slice || (p_ty && is_16byte_type(p_ty))) {
            current_offset += 16;
        } else {
            current_offset += 8;
        }
    }
    current_offset = (current_offset + 15) & ~15;

    uint32_t max_offset = current_offset;

    std::function<void(const ASTStmt*, uint32_t&)> scan_stmt;
    std::function<void(const ASTExpr*, uint32_t&)> scan_expr;

    scan_expr = [&](const ASTExpr* expr, uint32_t& off) {
        if (!expr) return;
        if (expr->kind == ExprKind::Match) {
            off += 8;
            max_offset = std::max(max_offset, off);
            for (const auto& arm : expr->match_arms) {
                scan_expr(arm.condition, off);
                for (const auto* s : arm.statements) scan_stmt(s, off);
                scan_expr(arm.result_expr, off);
            }
            return;
        }
        if (expr->kind == ExprKind::Call && current_program_) {
            if (expr->left && expr->left->kind == ExprKind::Identifier) {
                std::string callee = std::string(expr->left->raw_text);
                for (const auto* f : current_program_->functions) {
                    if ((f->name == callee || f->name.ends_with("::" + callee)) && f->has_variadic_slice) {
                        size_t fixed_count = f->params.size() - 1;
                        if (expr->args.size() >= fixed_count) {
                            size_t var_count = expr->args.size() - fixed_count;
                            off += (uint32_t)(var_count * 16 + 16);
                            max_offset = std::max(max_offset, off);
                        }
                        break;
                    }
                }
            }
        }
        scan_expr(expr->left, off);
        scan_expr(expr->right, off);
        for (const auto* a : expr->args) scan_expr(a, off);
        for (const auto& sf : expr->struct_fields) scan_expr(sf.second, off);
    };

    scan_stmt = [&](const ASTStmt* stmt, uint32_t& off) {
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
                off += sz;
                max_offset = std::max(max_offset, off);
                scan_expr(stmt->init_expr, off);
                break;
            }
            case StmtKind::If:
                scan_expr(stmt->condition, off);
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                for (const auto* s : stmt->else_block) scan_stmt(s, off);
                break;
            case StmtKind::HashIf:
                scan_expr(stmt->condition, off);
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                for (const auto* s : stmt->else_block) scan_stmt(s, off);
                break;
            case StmtKind::Defer:
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                break;
            case StmtKind::While:
            case StmtKind::DoWhile:
                scan_expr(stmt->condition, off);
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                break;
            case StmtKind::For:
                if (stmt->init_stmt) scan_stmt(stmt->init_stmt, off);
                scan_expr(stmt->condition, off);
                if (stmt->step_stmt) scan_stmt(stmt->step_stmt, off);
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                break;
            case StmtKind::Switch:
                scan_expr(stmt->condition, off);
                for (const auto& sc : stmt->switch_cases) {
                    scan_expr(sc.match_val, off);
                    for (const auto* s : sc.body) scan_stmt(s, off);
                }
                break;
            case StmtKind::Foreach:
                scan_expr(stmt->iter_collection, off);
                off += 32;
                max_offset = std::max(max_offset, off);
                for (const auto* s : stmt->then_block) scan_stmt(s, off);
                break;
            case StmtKind::ResultBranch:
                scan_expr(stmt->condition, off);
                off += 16;
                max_offset = std::max(max_offset, off);
                for (const auto* s : stmt->success_block) scan_stmt(s, off);
                for (const auto* s : stmt->failure_block) scan_stmt(s, off);
                break;
            case StmtKind::Assignment:
            case StmtKind::CompoundAssignment:
                scan_expr(stmt->target_expr, off);
                scan_expr(stmt->value_expr, off);
                break;
            case StmtKind::Increment:
            case StmtKind::Decrement:
                scan_expr(stmt->target_expr, off);
                break;
            case StmtKind::Return:
            case StmtKind::ExprStmt:
                scan_expr(stmt->value_expr, off);
                break;
            default:
                break;
        }
    };

    uint32_t running_off = current_offset;
    for (const auto* s : fn->body) {
        scan_stmt(s, running_off);
    }

    uint32_t total = max_offset + 128;
    if (target_os_ == TargetOS::Windows) {
        total += 64; // Additional shadow space buffer
    }
    total = (total + 15) & ~15;
    return std::max(total, 64u);
}

SemaType* NasmEmitter::get_member_type(const ASTExpr* expr) {
    if (!expr || expr->kind != ExprKind::MemberAccess) return nullptr;
    auto* base_expr = expr->left;
    if (!base_expr) return nullptr;
    
    SemaType* base_t = get_expr_type(base_expr);
    if (base_t) {
        if (base_t->kind == SemaType::Kind::Pointer) {
            base_t = std::get<PointerTypeInfo>(base_t->data).pointee;
        }
        if (base_t && base_t->kind == SemaType::Kind::Struct) {
            auto& s_info = std::get<StructTypeInfo>(base_t->data);
            auto f_it = s_info.field_map.find(std::string(expr->raw_text));
            if (f_it != s_info.field_map.end()) {
                return s_info.fields[f_it->second].type;
            }
        }
        if (base_t && base_t->kind == SemaType::Kind::Union) {
            auto& u_info = std::get<UnionTypeInfo>(base_t->data);
            auto f_it = u_info.field_map.find(std::string(expr->raw_text));
            if (f_it != u_info.field_map.end()) {
                return u_info.fields[f_it->second].type;
            }
        }
    }
    return nullptr;
}

SemaType* NasmEmitter::get_expr_type(const ASTExpr* expr) {
    if (!expr) return nullptr;
    if (expr->kind == ExprKind::Identifier) {
        auto it = local_vars_.find(std::string(expr->raw_text));
        if (it != local_vars_.end()) return it->second.type;
    }
    if (expr->kind == ExprKind::MemberAccess) {
        return get_member_type(expr);
    }
    if (expr->kind == ExprKind::Index) {
        auto* base_ty = get_expr_type(expr->left);
        if (base_ty) {
            if (base_ty->kind == SemaType::Kind::Array) {
                return std::get<ArrayTypeInfo>(base_ty->data).element;
            }
            if (base_ty->kind == SemaType::Kind::Slice) {
                return std::get<SliceTypeInfo>(base_ty->data).element;
            }
            if (base_ty->kind == SemaType::Kind::Pointer) {
                return std::get<PointerTypeInfo>(base_ty->data).pointee;
            }
            if (base_ty->is_primitive(TokenKind::KwString8)) {
                auto it = type_env_.find("char8");
                return it != type_env_.end() ? it->second : nullptr;
            }
            if (base_ty->is_primitive(TokenKind::KwString16)) {
                auto it = type_env_.find("char16");
                return it != type_env_.end() ? it->second : nullptr;
            }
            if (base_ty->is_primitive(TokenKind::KwString32)) {
                auto it = type_env_.find("char32");
                return it != type_env_.end() ? it->second : nullptr;
            }
        }
    }
    if (expr->kind == ExprKind::Cast || expr->kind == ExprKind::BuiltinBitcast) {
        return resolve_type_node(expr->target_type);
    }
    return nullptr;
}

bool NasmEmitter::is_float_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Literal) {
        if (expr->raw_text == "null" || expr->raw_text == "true" || expr->raw_text == "false") {
            return false;
        }
        if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`' || expr->raw_text.front() == '\'') {
            return false;
        }
        return (expr->raw_text.find('.') != std::string_view::npos ||
                expr->raw_text.find('e') != std::string_view::npos ||
                expr->raw_text.find('E') != std::string_view::npos);
    }
    if (expr->kind == ExprKind::Cast) {
        auto* target_ty = resolve_type_node(expr->target_type);
        return target_ty && target_ty->is_floating_point();
    }
    if (expr->kind == ExprKind::Identifier) {
        if (float_const_defs_.find(std::string(expr->raw_text)) != float_const_defs_.end()) {
            return true;
        }
        auto it = local_vars_.find(std::string(expr->raw_text));
        if (it != local_vars_.end() && it->second.type && it->second.type->is_floating_point()) {
            return true;
        }
        return false;
    }
    if (expr->kind == ExprKind::MemberAccess) {
        auto* mem_ty = get_member_type(expr);
        return mem_ty && mem_ty->is_floating_point();
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
                if (fn->name == callee || fn->name.ends_with("::" + callee)) {
                    auto* ret_ty = resolve_type_node(fn->return_type);
                    if (ret_ty && ret_ty->is_floating_point()) return true;
                }
            }
        }
    }
    return false;
}

bool NasmEmitter::is_128bit_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Cast) {
        auto* tgt = resolve_type_node(expr->target_type);
        return tgt && tgt->is_128bit();
    }
    if (expr->kind == ExprKind::Identifier) {
        auto it = local_vars_.find(std::string(expr->raw_text));
        if (it != local_vars_.end() && it->second.type && it->second.type->is_128bit()) {
            return true;
        }
        return false;
    }
    if (expr->kind == ExprKind::MemberAccess) {
        auto* mem_ty = get_member_type(expr);
        return mem_ty && mem_ty->is_128bit();
    }
    if (expr->kind == ExprKind::Binary) {
        if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "&" || expr->op == "|" || expr->op == "^") {
            return is_128bit_expr(expr->left) || is_128bit_expr(expr->right);
        }
        return false;
    }
    if (expr->kind == ExprKind::Call) {
        if (current_program_ && expr->left && expr->left->kind == ExprKind::Identifier) {
            std::string callee = std::string(expr->left->raw_text);
            for (const auto* fn : current_program_->functions) {
                if (fn->name == callee || fn->name.ends_with("::" + callee)) {
                    auto* ret_ty = resolve_type_node(fn->return_type);
                    if (ret_ty && ret_ty->is_128bit()) return true;
                }
            }
        }
    }
    return false;
}

bool NasmEmitter::is_64bit_expr(const ASTExpr* expr) {
    if (!expr) return false;
    if (expr->kind == ExprKind::Binary) {
        if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "/" || 
            expr->op == "%" || expr->op == "&" || expr->op == "|" || expr->op == "^" || 
            expr->op == "<<" || expr->op == ">>") {
            return is_64bit_expr(expr->left) || is_64bit_expr(expr->right);
        }
        return false;
    }
    if (expr->kind == ExprKind::Unary) {
        if (expr->op == "-" || expr->op == "~" || expr->op == "+") {
            return is_64bit_expr(expr->left);
        }
    }
    if (expr->kind == ExprKind::Call) {
        if (current_program_ && expr->left && expr->left->kind == ExprKind::Identifier) {
            std::string callee = std::string(expr->left->raw_text);
            for (const auto* fn : current_program_->functions) {
                if (fn->name == callee || fn->name.ends_with("::" + callee)) {
                    auto* ret_ty = resolve_type_node(fn->return_type);
                    if (ret_ty && (ret_ty->size_bytes == 8 && !ret_ty->is_floating_point())) return true;
                }
            }
        }
    }
    auto* ty = get_expr_type(expr);
    if (ty) {
        if (ty->kind == SemaType::Kind::Pointer) return true;
        if (ty->size_bytes == 8 && !ty->is_floating_point()) return true;
    }
    if (expr->kind == ExprKind::Identifier) {
        auto it = local_vars_.find(std::string(expr->raw_text));
        if (it != local_vars_.end() && it->second.type) {
            auto* vty = it->second.type;
            if (vty->kind == SemaType::Kind::Pointer) return true;
            if (vty->size_bytes == 8 && !vty->is_floating_point()) return true;
        }
    }
    if (expr->kind == ExprKind::Cast) {
        auto* tgt = resolve_type_node(expr->target_type);
        if (tgt) {
            if (tgt->kind == SemaType::Kind::Pointer) return true;
            if (tgt->size_bytes == 8 && !tgt->is_floating_point()) return true;
        }
    }
    return false;
}

bool NasmEmitter::is_slice_expr(const ASTExpr* expr) {
    if (!expr) return false;
    auto* ty = get_expr_type(expr);
    return ty && ty->kind == SemaType::Kind::Slice;
}

static std::string eval_const_string_emitter(const ASTExpr* expr, TargetOS target_os) {
    if (!expr) return "";
    if (expr->kind == ExprKind::Literal) {
        if (expr->raw_text.size() >= 2 && (expr->raw_text.front() == '"' || expr->raw_text.front() == '`')) {
            return std::string(expr->raw_text.substr(1, expr->raw_text.size() - 2));
        }
        return "";
    }
    if (expr->kind == ExprKind::BuiltinTarget) {
        return (target_os == TargetOS::Windows) ? "x86_64-windows" : "x86_64-linux";
    }
    if (expr->kind == ExprKind::BuiltinArch) return "x86_64";
    if (expr->kind == ExprKind::BuiltinEndian) return "little";
    return "";
}

int64_t NasmEmitter::eval_const_expr(const ASTExpr* expr) {
    if (!expr) return 0;
    if (expr->kind == ExprKind::Literal) {
        return parse_literal_int_emitter(expr->raw_text);
    }
    if (expr->kind == ExprKind::BuiltinLine) {
        return expr->evaluated_line;
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
        if (expr->op == "==" || expr->op == "!=") {
            bool left_is_str = (expr->left && (expr->left->kind == ExprKind::BuiltinTarget || expr->left->kind == ExprKind::BuiltinArch || expr->left->kind == ExprKind::BuiltinEndian || (expr->left->kind == ExprKind::Literal && !expr->left->raw_text.empty() && (expr->left->raw_text.front() == '"' || expr->left->raw_text.front() == '`'))));
            bool right_is_str = (expr->right && (expr->right->kind == ExprKind::BuiltinTarget || expr->right->kind == ExprKind::BuiltinArch || expr->right->kind == ExprKind::BuiltinEndian || (expr->right->kind == ExprKind::Literal && !expr->right->raw_text.empty() && (expr->right->raw_text.front() == '"' || expr->right->raw_text.front() == '`'))));
            if (left_is_str || right_is_str) {
                std::string sl = eval_const_string_emitter(expr->left, target_os_);
                std::string sr = eval_const_string_emitter(expr->right, target_os_);
                if (expr->op == "==") return sl == sr ? 1 : 0;
                if (expr->op == "!=") return sl != sr ? 1 : 0;
            }
        }
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
        if (ast_ty->primitive_kind == TokenKind::KwAny) {
            auto it = type_env_.find("any");
            return it != type_env_.end() ? it->second : nullptr;
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
    rodata_sec_ << "str_bounds_panic: db \"Slice index out of bounds\", 10, 0\n";
    data_sec_   << "section .data\n";

    std::unordered_set<std::string> extern_declared;
    extern_declared.insert("strcmp");
    text_sec_ << "extern strcmp\n";

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
    defer_scopes_.clear();
    defer_scopes_.push_back({});

    std::string fn_lbl = sanitize_symbol_raw(fn->name);
    uint32_t frame_size = calculate_function_stack_size(fn);

    if (fn->is_exported || fn->name == "main") {
        text_sec_ << "global " << fn_lbl << "\n";
    }
    text_sec_ << fn_lbl << ":\n";
    if (target_os_ == TargetOS::Windows) {
        text_sec_ << "    push rbp\n";
        text_sec_ << "    push rsi\n";
        text_sec_ << "    push rdi\n";
        text_sec_ << "    mov rbp, rsp\n";
        text_sec_ << "    sub rsp, " << frame_size << "\n";
    } else {
        text_sec_ << "    push rbp\n";
        text_sec_ << "    mov rbp, rsp\n";
        text_sec_ << "    sub rsp, " << frame_size << "\n";
    }

    const char* int_arg_regs_sysv[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
    const char* int_arg_regs_ms64[] = { "rcx", "rdx", "r8", "r9" };

    size_t int_idx = 0;
    size_t flt_idx = 0;
    uint32_t stack_off = 0;

    for (size_t i = 0; i < fn->params.size(); ++i) {
        auto* p_ty = resolve_type_node(fn->params[i].type);
        if (!p_ty) {
            auto it = type_env_.find("int32");
            p_ty = it != type_env_.end() ? it->second : nullptr;
        }

        bool is_16b = fn->params[i].is_variadic_slice || (p_ty && is_16byte_type(p_ty));

        if (target_os_ == TargetOS::Windows) {
            if (is_16b) {
                stack_off += 16;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (int_idx + 1 < 4) {
                    text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs_ms64[int_idx] << "\n";
                    text_sec_ << "    mov [rbp - " << (stack_off - 8) << "], " << int_arg_regs_ms64[int_idx + 1] << "\n";
                    int_idx += 2;
                } else if (int_idx < 4) {
                    text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs_ms64[int_idx++] << "\n";
                    text_sec_ << "    mov rax, [rbp + 64]\n";
                    text_sec_ << "    mov [rbp - " << (stack_off - 8) << "], rax\n";
                }
            } else if (p_ty && p_ty->is_floating_point()) {
                stack_off += 8;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (flt_idx < 4) {
                    if (p_ty->size_bytes == 4) text_sec_ << "    movss [rbp - " << stack_off << "], xmm" << flt_idx << "\n";
                    else text_sec_ << "    movsd [rbp - " << stack_off << "], xmm" << flt_idx << "\n";
                    flt_idx++;
                    int_idx++;
                } else {
                    text_sec_ << "    movsd xmm0, [rbp + " << (64 + (i - 4) * 8) << "]\n";
                    text_sec_ << "    movsd [rbp - " << stack_off << "], xmm0\n";
                }
            } else {
                stack_off += 8;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (int_idx < 4) {
                    text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs_ms64[int_idx++] << "\n";
                } else {
                    text_sec_ << "    mov rax, [rbp + " << (64 + (i - 4) * 8) << "]\n";
                    text_sec_ << "    mov [rbp - " << stack_off << "], rax\n";
                }
            }
        } else {
            if (is_16b) {
                stack_off += 16;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (int_idx + 1 < 6) {
                    text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs_sysv[int_idx] << "\n";
                    text_sec_ << "    mov [rbp - " << (stack_off - 8) << "], " << int_arg_regs_sysv[int_idx + 1] << "\n";
                    int_idx += 2;
                }
            } else if (p_ty && p_ty->is_floating_point()) {
                stack_off += 8;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (flt_idx < 8) {
                    if (p_ty->size_bytes == 4) text_sec_ << "    movss [rbp - " << stack_off << "], xmm" << flt_idx++ << "\n";
                    else text_sec_ << "    movsd [rbp - " << stack_off << "], xmm" << flt_idx++ << "\n";
                }
            } else {
                stack_off += 8;
                VarInfo vi{ stack_off, p_ty };
                local_vars_[std::string(fn->params[i].name)] = vi;
                if (int_idx < 6) {
                    text_sec_ << "    mov [rbp - " << stack_off << "], " << int_arg_regs_sysv[int_idx++] << "\n";
                }
            }
        }
    }

    stack_off = (stack_off + 15) & ~15;
    current_stack_offset_ = stack_off;

    for (const auto* stmt : fn->body) {
        emit_statement(stmt, stack_off);
    }

    emit_deferred_statements(stack_off);

    if (target_os_ == TargetOS::Windows) {
        text_sec_ << "    mov rsp, rbp\n";
        text_sec_ << "    pop rdi\n";
        text_sec_ << "    pop rsi\n";
        text_sec_ << "    pop rbp\n";
        text_sec_ << "    ret\n\n";
    } else {
        text_sec_ << "    mov rsp, rbp\n";
        text_sec_ << "    pop rbp\n";
        text_sec_ << "    ret\n\n";
    }
}

void NasmEmitter::emit_deferred_statements(uint32_t& stack_offset) {
    for (auto scope_it = defer_scopes_.rbegin(); scope_it != defer_scopes_.rend(); ++scope_it) {
        for (auto it = scope_it->rbegin(); it != scope_it->rend(); ++it) {
            emit_statement(*it, stack_offset);
        }
    }
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
                    } else if (it->second.type->kind == SemaType::Kind::Struct || it->second.type->kind == SemaType::Kind::Union) {
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
            uint64_t arr_bound = 0;
            auto* base_ty = get_expr_type(lval->left);
            if (base_ty) {
                if (base_ty->kind == SemaType::Kind::Slice) {
                    is_slice = true;
                } else if (base_ty->kind == SemaType::Kind::Array) {
                    arr_bound = std::get<ArrayTypeInfo>(base_ty->data).size;
                }
            }

            emit_expression(lval->right);
            text_sec_ << "    mov ebx, eax\n";
            text_sec_ << "    pop rax\n";

            if (enable_bounds_checks_) {
                std::string ok_lbl = next_label("L_bounds_ok");
                if (is_slice) {
                    text_sec_ << "    mov rcx, [rax + 8]\n";
                    text_sec_ << "    cmp rbx, rcx\n";
                    text_sec_ << "    jb " << ok_lbl << "\n";
                    text_sec_ << "    lea rdi, [rel str_bounds_panic]\n";
                    text_sec_ << "    mov rsi, 27\n";
                    text_sec_ << "    call __builtin_panic\n";
                    text_sec_ << ok_lbl << ":\n";
                } else if (arr_bound > 0) {
                    text_sec_ << "    cmp rbx, " << arr_bound << "\n";
                    text_sec_ << "    jb " << ok_lbl << "\n";
                    text_sec_ << "    lea rdi, [rel str_bounds_panic]\n";
                    text_sec_ << "    mov rsi, 27\n";
                    text_sec_ << "    call __builtin_panic\n";
                    text_sec_ << ok_lbl << ":\n";
                }
            }

            if (is_slice || (base_ty && (base_ty->kind == SemaType::Kind::Pointer || 
                             base_ty->is_primitive(TokenKind::KwString8) || 
                             base_ty->is_primitive(TokenKind::KwString16) || 
                             base_ty->is_primitive(TokenKind::KwString32)))) {
                text_sec_ << "    mov rax, [rax]\n";
            }

            auto* elem_ty = get_expr_type(lval);
            uint32_t elem_sz = elem_ty ? elem_ty->size_bytes : 4;

            text_sec_ << "    movsxd rbx, ebx\n";
            if (elem_sz == 1) {
                // byte indexing
            } else if (elem_sz == 2) {
                text_sec_ << "    shl rbx, 1\n";
            } else if (elem_sz == 4) {
                text_sec_ << "    shl rbx, 2\n";
            } else if (elem_sz == 8) {
                text_sec_ << "    shl rbx, 3\n";
            } else if (elem_sz == 16) {
                text_sec_ << "    shl rbx, 4\n";
            } else {
                text_sec_ << "    imul rbx, " << elem_sz << "\n";
            }
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

    current_stack_offset_ = stack_offset;

    switch (stmt->kind) {
        case StmtKind::Defer: {
            for (auto* s : stmt->then_block) {
                if (!defer_scopes_.empty()) {
                    defer_scopes_.back().push_back(s);
                }
            }
            break;
        }
        case StmtKind::VarDecl: {
            auto* v_ty = resolve_type_node(stmt->type_annot);
            if (!v_ty) {
                auto it = type_env_.find("int32");
                v_ty = it != type_env_.end() ? it->second : nullptr;
            }

            uint32_t sz = v_ty ? v_ty->size_bytes : 4;
            sz = (sz + 7) & ~7;
            stack_offset += sz;
            current_stack_offset_ = stack_offset;

            VarInfo vi{ stack_offset, v_ty };
            local_vars_[std::string(stmt->name)] = vi;

            if (stmt->init_expr) {
                if (stmt->init_expr->kind == ExprKind::StructLiteral && v_ty && (v_ty->kind == SemaType::Kind::Struct || v_ty->kind == SemaType::Kind::Union)) {
                    if (v_ty->kind == SemaType::Kind::Struct) {
                        auto& s_info = std::get<StructTypeInfo>(v_ty->data);
                        for (auto& sf : stmt->init_expr->struct_fields) {
                            auto f_it = s_info.field_map.find(std::string(sf.first));
                            if (f_it != s_info.field_map.end()) {
                                uint32_t f_off = s_info.fields[f_it->second].offset;
                                auto* f_ty = s_info.fields[f_it->second].type;
                                emit_expression(sf.second);
                                if (f_ty && (f_ty->kind == SemaType::Kind::Struct || f_ty->kind == SemaType::Kind::Union)) {
                                    text_sec_ << "    mov rsi, rax\n";
                                    text_sec_ << "    lea rdi, [rbp - " << (stack_offset - f_off) << "]\n";
                                    for (uint32_t b = 0; b < f_ty->size_bytes; b += 8) {
                                        text_sec_ << "    mov r8, [rsi + " << b << "]\n";
                                        text_sec_ << "    mov [rdi + " << b << "], r8\n";
                                    }
                                } else if (f_ty && f_ty->is_floating_point()) {
                                    if (f_ty->size_bytes == 4) text_sec_ << "    movss [rbp - " << (stack_offset - f_off) << "], xmm0\n";
                                    else text_sec_ << "    movsd [rbp - " << (stack_offset - f_off) << "], xmm0\n";
                                } else if (f_ty && f_ty->size_bytes == 8) {
                                    text_sec_ << "    mov [rbp - " << (stack_offset - f_off) << "], rax\n";
                                } else {
                                    text_sec_ << "    mov [rbp - " << (stack_offset - f_off) << "], eax\n";
                                }
                            }
                        }
                    } else if (v_ty->kind == SemaType::Kind::Union) {
                        auto& u_info = std::get<UnionTypeInfo>(v_ty->data);
                        for (auto& sf : stmt->init_expr->struct_fields) {
                            auto f_it = u_info.field_map.find(std::string(sf.first));
                            if (f_it != u_info.field_map.end()) {
                                auto* f_ty = u_info.fields[f_it->second].type;
                                emit_expression(sf.second);
                                if (f_ty && f_ty->is_floating_point()) {
                                    if (f_ty->size_bytes == 4) text_sec_ << "    movss [rbp - " << stack_offset << "], xmm0\n";
                                    else text_sec_ << "    movsd [rbp - " << stack_offset << "], xmm0\n";
                                } else if (f_ty && f_ty->size_bytes == 8) {
                                    text_sec_ << "    mov [rbp - " << stack_offset << "], rax\n";
                                } else {
                                    text_sec_ << "    mov [rbp - " << stack_offset << "], eax\n";
                                }
                            }
                        }
                    }
                } else if (stmt->init_expr->kind == ExprKind::ArrayLiteral) {
                    if (stmt->init_expr->is_repeat_fill && v_ty && v_ty->kind == SemaType::Kind::Array) {
                        uint64_t arr_len = std::get<ArrayTypeInfo>(v_ty->data).size;
                        emit_expression(stmt->init_expr->args[0]);
                        for (uint64_t i = 0; i < arr_len; ++i) {
                            text_sec_ << "    mov [rbp - " << (stack_offset - i * 4) << "], eax\n";
                        }
                    } else {
                        for (size_t i = 0; i < stmt->init_expr->args.size(); ++i) {
                            emit_expression(stmt->init_expr->args[i]);
                            text_sec_ << "    mov [rbp - " << (stack_offset - i * 4) << "], eax\n";
                        }
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
                } else if (v_ty && (v_ty->kind == SemaType::Kind::Struct || v_ty->kind == SemaType::Kind::Union)) {
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
                } else if (v_ty && is_16byte_type(v_ty)) {
                    emit_expression(stmt->init_expr);
                    text_sec_ << "    mov [rbp - " << stack_offset << "], rax\n";
                    text_sec_ << "    mov [rbp - " << (stack_offset - 8) << "], rdx\n";
                } else {
                    emit_expression(stmt->init_expr);
                    if (v_ty && (v_ty->kind == SemaType::Kind::Pointer || v_ty->size_bytes == 8)) {
                        text_sec_ << "    mov [rbp - " << stack_offset << "], rax\n";
                    } else if (v_ty && v_ty->size_bytes == 2) {
                        text_sec_ << "    mov [rbp - " << stack_offset << "], ax\n";
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
            } else if (is_16byte_expr(stmt->target_expr) || is_16byte_expr(stmt->value_expr)) {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rdx\n    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n    pop rcx\n";
                text_sec_ << "    mov [rax], rbx\n";
                text_sec_ << "    mov [rax + 8], rcx\n";
            } else {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n";
                auto* target_ty = get_expr_type(stmt->target_expr);
                if (target_ty && target_ty->size_bytes == 8) {
                    text_sec_ << "    mov [rax], rbx\n";
                } else if (target_ty && target_ty->size_bytes == 2) {
                    text_sec_ << "    mov [rax], bx\n";
                } else if (target_ty && target_ty->size_bytes == 1) {
                    text_sec_ << "    mov [rax], bl\n";
                } else {
                    text_sec_ << "    mov [rax], ebx\n";
                }
            }
            break;
        }
        case StmtKind::CompoundAssignment: {
            if (is_float_expr(stmt->target_expr)) {
                emit_expression(stmt->value_expr);
                text_sec_ << "    sub rsp, 8\n    movsd [rsp], xmm0\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    movsd xmm1, [rsp]\n    add rsp, 8\n";
                text_sec_ << "    movsd xmm0, [rax]\n";
                if (stmt->op == "+=")      text_sec_ << "    addsd xmm0, xmm1\n";
                else if (stmt->op == "-=") text_sec_ << "    subsd xmm0, xmm1\n";
                else if (stmt->op == "*=") text_sec_ << "    mulsd xmm0, xmm1\n";
                else if (stmt->op == "/=") text_sec_ << "    divsd xmm0, xmm1\n";
                text_sec_ << "    movsd [rax], xmm0\n";
            } else if (is_128bit_expr(stmt->target_expr)) {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rdx\n    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n    pop rcx\n";
                text_sec_ << "    mov rsi, [rax]\n";
                text_sec_ << "    mov rdi, [rax + 8]\n";
                if (stmt->op == "+=") {
                    text_sec_ << "    add rsi, rbx\n    adc rdi, rcx\n";
                } else if (stmt->op == "-=") {
                    text_sec_ << "    sub rsi, rbx\n    sbb rdi, rcx\n";
                } else if (stmt->op == "&=") {
                    text_sec_ << "    and rsi, rbx\n    and rdi, rcx\n";
                } else if (stmt->op == "|=") {
                    text_sec_ << "    or rsi, rbx\n    or rdi, rcx\n";
                } else if (stmt->op == "^=") {
                    text_sec_ << "    xor rsi, rbx\n    xor rdi, rcx\n";
                }
                text_sec_ << "    mov [rax], rsi\n";
                text_sec_ << "    mov [rax + 8], rdi\n";
            } else if (is_64bit_expr(stmt->target_expr)) {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n";
                text_sec_ << "    mov rcx, [rax]\n";

                if (stmt->op == "+=")      text_sec_ << "    add rcx, rbx\n";
                else if (stmt->op == "-=") text_sec_ << "    sub rcx, rbx\n";
                else if (stmt->op == "*=") text_sec_ << "    imul rcx, rbx\n";
                else if (stmt->op == "/=") text_sec_ << "    mov rax, rcx\n    cqo\n    idiv rbx\n    mov rcx, rax\n";
                else if (stmt->op == "%=") text_sec_ << "    mov rax, rcx\n    cqo\n    idiv rbx\n    mov rcx, rdx\n";
                else if (stmt->op == "&=") text_sec_ << "    and rcx, rbx\n";
                else if (stmt->op == "|=") text_sec_ << "    or rcx, rbx\n";
                else if (stmt->op == "^=") text_sec_ << "    xor rcx, rbx\n";
                else if (stmt->op == "<<=") text_sec_ << "    push rcx\n    mov cl, bl\n    pop rax\n    shl rax, cl\n    mov rcx, rax\n";
                else if (stmt->op == ">>=") text_sec_ << "    push rcx\n    mov cl, bl\n    pop rax\n    sar rax, cl\n    mov rcx, rax\n";

                text_sec_ << "    mov [rax], rcx\n";
            } else {
                emit_expression(stmt->value_expr);
                text_sec_ << "    push rax\n";
                emit_lvalue_address(stmt->target_expr);
                text_sec_ << "    pop rbx\n";
                auto* target_ty = get_expr_type(stmt->target_expr);
                uint32_t target_sz = target_ty ? target_ty->size_bytes : 4;
                if (target_sz == 1) {
                    text_sec_ << "    movzx ecx, byte [rax]\n";
                } else if (target_sz == 2) {
                    text_sec_ << "    movzx ecx, word [rax]\n";
                } else {
                    text_sec_ << "    mov ecx, [rax]\n";
                }

                if (stmt->op == "+=")      text_sec_ << "    add ecx, ebx\n";
                else if (stmt->op == "-=") text_sec_ << "    sub ecx, ebx\n";
                else if (stmt->op == "*=") text_sec_ << "    imul ecx, ebx\n";
                else if (stmt->op == "/=") text_sec_ << "    mov eax, ecx\n    cdq\n    idiv ebx\n    mov ecx, eax\n";
                else if (stmt->op == "%=") text_sec_ << "    mov eax, ecx\n    cdq\n    idiv ebx\n    mov ecx, edx\n";
                else if (stmt->op == "&=") text_sec_ << "    and ecx, ebx\n";
                else if (stmt->op == "|=") text_sec_ << "    or ecx, ebx\n";
                else if (stmt->op == "^=") text_sec_ << "    xor ecx, ebx\n";
                else if (stmt->op == "<<=") text_sec_ << "    push rcx\n    mov cl, bl\n    pop eax\n    shl eax, cl\n    mov ecx, eax\n";
                else if (stmt->op == ">>=") text_sec_ << "    push rcx\n    mov cl, bl\n    pop eax\n    sar eax, cl\n    mov ecx, eax\n";

                if (target_sz == 1) {
                    text_sec_ << "    mov [rax], cl\n";
                } else if (target_sz == 2) {
                    text_sec_ << "    mov [rax], cx\n";
                } else {
                    text_sec_ << "    mov [rax], ecx\n";
                }
            }
            break;
        }
        case StmtKind::Increment: {
            emit_lvalue_address(stmt->target_expr);
            auto* target_ty = get_expr_type(stmt->target_expr);
            if (target_ty && (target_ty->size_bytes == 8 || target_ty->kind == SemaType::Kind::Pointer)) {
                text_sec_ << "    inc qword [rax]\n";
            } else if (target_ty && target_ty->size_bytes == 2) {
                text_sec_ << "    inc word [rax]\n";
            } else if (target_ty && target_ty->size_bytes == 1) {
                text_sec_ << "    inc byte [rax]\n";
            } else {
                text_sec_ << "    inc dword [rax]\n";
            }
            break;
        }
        case StmtKind::Decrement: {
            emit_lvalue_address(stmt->target_expr);
            auto* target_ty = get_expr_type(stmt->target_expr);
            if (target_ty && (target_ty->size_bytes == 8 || target_ty->kind == SemaType::Kind::Pointer)) {
                text_sec_ << "    dec qword [rax]\n";
            } else if (target_ty && target_ty->size_bytes == 2) {
                text_sec_ << "    dec word [rax]\n";
            } else if (target_ty && target_ty->size_bytes == 1) {
                text_sec_ << "    dec byte [rax]\n";
            } else {
                text_sec_ << "    dec dword [rax]\n";
            }
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
        case StmtKind::For: {
            std::string cond_lbl  = next_label("L_for_cond");
            std::string step_lbl  = next_label("L_for_step");
            std::string break_lbl = next_label("L_for_break");

            loop_stack_.push_back({step_lbl, break_lbl});

            if (stmt->init_stmt) {
                emit_statement(stmt->init_stmt, stack_offset);
            }

            text_sec_ << cond_lbl << ":\n";
            if (stmt->condition) {
                emit_expression(stmt->condition);
                text_sec_ << "    cmp eax, 0\n    je " << break_lbl << "\n";
            }

            for (auto* s : stmt->then_block) emit_statement(s, stack_offset);

            text_sec_ << step_lbl << ":\n";
            if (stmt->step_stmt) {
                emit_statement(stmt->step_stmt, stack_offset);
            }

            text_sec_ << "    jmp " << cond_lbl << "\n";
            text_sec_ << break_lbl << ":\n";

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
            current_stack_offset_ = stack_offset;

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
            uint32_t s_off = stack_offset;
            current_stack_offset_ = stack_offset;
            local_vars_[std::string(stmt->success_var)] = VarInfo{ s_off, type_env_.find("int32")->second };
            text_sec_ << "    mov [rbp - " << s_off << "], eax\n";
            for (auto* s : stmt->success_block) emit_statement(s, stack_offset);
            text_sec_ << "    jmp " << end_lbl << "\n";

            text_sec_ << fail_lbl << ":\n";
            stack_offset += 8;
            uint32_t f_off = stack_offset;
            current_stack_offset_ = stack_offset;
            local_vars_[std::string(stmt->failure_var)] = VarInfo{ f_off, type_env_.find("int32")->second };
            text_sec_ << "    mov [rbp - " << f_off << "], edx\n";
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
            bool has_defers = false;
            for (const auto& scope : defer_scopes_) {
                if (!scope.empty()) { has_defers = true; break; }
            }
            if (has_defers) {
                text_sec_ << "    push rdx\n    push rax\n    sub rsp, 8\n    movsd [rsp], xmm0\n";
                emit_deferred_statements(stack_offset);
                text_sec_ << "    movsd xmm0, [rsp]\n    add rsp, 8\n    pop rax\n    pop rdx\n";
            }
            if (target_os_ == TargetOS::Windows) {
                text_sec_ << "    mov rsp, rbp\n";
                text_sec_ << "    pop rdi\n";
                text_sec_ << "    pop rsi\n";
                text_sec_ << "    pop rbp\n";
                text_sec_ << "    ret\n";
            } else {
                text_sec_ << "    mov rsp, rbp\n    pop rbp\n    ret\n";
            }
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
            if (expr->raw_text == "null") {
                text_sec_ << "    xor eax, eax\n";
            } else if (expr->raw_text == "true") {
                text_sec_ << "    mov eax, 1\n";
            } else if (expr->raw_text == "false") {
                text_sec_ << "    xor eax, eax\n";
            } else if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`') {
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
            } else if (expr->raw_text.front() == '\'') {
                int64_t c_val = parse_literal_int_emitter(expr->raw_text);
                text_sec_ << "    mov eax, " << c_val << "\n";
            } else {
                int64_t val = parse_literal_int_emitter(expr->raw_text);
                text_sec_ << "    mov rax, " << val << "\n";
            }
            break;
        }
        case ExprKind::BuiltinFile:
        case ExprKind::BuiltinTarget:
        case ExprKind::BuiltinArch:
        case ExprKind::BuiltinEndian: {
            std::string str_lbl = next_label("L_refl_str");
            rodata_sec_ << str_lbl << ": db ";
            for (char c : expr->raw_text) {
                rodata_sec_ << (int)(unsigned char)c << ", ";
            }
            rodata_sec_ << "0\n";
            text_sec_ << "    lea rax, [rel " << str_lbl << "]\n";
            break;
        }
        case ExprKind::BuiltinLine: {
            text_sec_ << "    mov eax, " << expr->evaluated_line << "\n";
            break;
        }
        case ExprKind::BuiltinTypeof: {
            auto* ty = get_expr_type(expr->left);
            std::string ty_name = ty ? TypeChecker::get_type_name(ty) : "unknown";
            std::string str_lbl = next_label("L_typeof_str");
            rodata_sec_ << str_lbl << ": db ";
            for (char c : ty_name) {
                rodata_sec_ << (int)(unsigned char)c << ", ";
            }
            rodata_sec_ << "0\n";
            text_sec_ << "    lea rax, [rel " << str_lbl << "]\n";
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
        case ExprKind::Cast: {
            bool src_is_flt = is_float_expr(expr->left);
            auto* dst_ty = resolve_type_node(expr->target_type);
            bool dst_is_flt = dst_ty && dst_ty->is_floating_point();
            auto* src_ty = get_expr_type(expr->left);

            emit_expression(expr->left);

            if (src_is_flt && !dst_is_flt) {
                if (dst_ty && dst_ty->size_bytes == 8) {
                    text_sec_ << "    cvttsd2si rax, xmm0\n";
                } else {
                    text_sec_ << "    cvttsd2si eax, xmm0\n";
                }
            } else if (!src_is_flt && dst_is_flt) {
                if (dst_ty && dst_ty->size_bytes == 4) {
                    text_sec_ << "    cvtsi2ss xmm0, rax\n";
                } else {
                    text_sec_ << "    cvtsi2sd xmm0, rax\n";
                }
            } else if (src_is_flt && dst_is_flt) {
                if (dst_ty && dst_ty->size_bytes == 4) {
                    text_sec_ << "    cvtsd2ss xmm0, xmm0\n";
                } else {
                    text_sec_ << "    cvtss2sd xmm0, xmm0\n";
                }
            } else if (dst_ty && dst_ty->is_128bit()) {
                if (dst_ty->is_primitive(TokenKind::KwUint128)) {
                    text_sec_ << "    xor edx, edx\n";
                } else {
                    text_sec_ << "    cqo\n";
                }
            } else {
                if (dst_ty && dst_ty->size_bytes == 8) {
                    if (src_ty && src_ty->size_bytes < 8) {
                        if (src_ty->is_primitive(TokenKind::KwUint8) || 
                            src_ty->is_primitive(TokenKind::KwUint16) || 
                            src_ty->is_primitive(TokenKind::KwUint32)) {
                            text_sec_ << "    mov eax, eax\n";
                        } else {
                            text_sec_ << "    movsxd rax, eax\n";
                        }
                    }
                } else if (dst_ty && dst_ty->size_bytes == 4) {
                    if (src_ty && src_ty->size_bytes == 8) {
                        text_sec_ << "    mov eax, eax\n";
                    }
                } else if (dst_ty && dst_ty->size_bytes == 2) {
                    text_sec_ << "    movzx eax, ax\n";
                } else if (dst_ty && dst_ty->size_bytes == 1) {
                    text_sec_ << "    movzx eax, al\n";
                }
            }
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

            auto fc_it = float_const_defs_.find(std::string(expr->raw_text));
            if (fc_it != float_const_defs_.end()) {
                std::string flt_lbl = next_label("L_fconst");
                rodata_sec_ << flt_lbl << ": dq " << std::setprecision(17) << fc_it->second << "\n";
                text_sec_ << "    movsd xmm0, [rel " << flt_lbl << "]\n";
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
                } else if (it->second.type && is_16byte_type(it->second.type)) {
                    text_sec_ << "    mov rax, [rbp - " << it->second.stack_offset << "]\n";
                    text_sec_ << "    mov rdx, [rbp - " << (it->second.stack_offset - 8) << "]\n";
                } else if (it->second.type && (it->second.type->kind == SemaType::Kind::Struct || it->second.type->kind == SemaType::Kind::Union || it->second.type->kind == SemaType::Kind::Array)) {
                    text_sec_ << "    lea rax, [rbp - " << it->second.stack_offset << "]\n";
                } else if (it->second.type && (it->second.type->kind == SemaType::Kind::Pointer || it->second.type->size_bytes == 8)) {
                    text_sec_ << "    mov rax, [rbp - " << it->second.stack_offset << "]\n";
                } else if (it->second.type && it->second.type->size_bytes == 2) {
                    text_sec_ << "    movzx eax, word [rbp - " << it->second.stack_offset << "]\n";
                } else if (it->second.type && it->second.type->size_bytes == 1) {
                    text_sec_ << "    movzx eax, byte [rbp - " << it->second.stack_offset << "]\n";
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
                auto* mem_ty = get_member_type(expr);
                if (mem_ty && mem_ty->is_floating_point()) {
                    if (mem_ty->size_bytes == 4) {
                        text_sec_ << "    movss xmm0, [rax]\n";
                    } else {
                        text_sec_ << "    movsd xmm0, [rax]\n";
                    }
                } else if (mem_ty && is_16byte_type(mem_ty)) {
                    text_sec_ << "    mov rdx, [rax + 8]\n";
                    text_sec_ << "    mov rax, [rax]\n";
                } else if (mem_ty && mem_ty->size_bytes == 8) {
                    text_sec_ << "    mov rax, [rax]\n";
                } else if (mem_ty && mem_ty->size_bytes == 2) {
                    text_sec_ << "    movzx eax, word [rax]\n";
                } else if (mem_ty && mem_ty->size_bytes == 1) {
                    text_sec_ << "    movzx eax, byte [rax]\n";
                } else {
                    text_sec_ << "    mov eax, [rax]\n";
                }
            }
            break;
        }
        case ExprKind::Index: {
            emit_lvalue_address(expr);
            auto* elem_ty = get_expr_type(expr);
            if (elem_ty && elem_ty->is_floating_point()) {
                if (elem_ty->size_bytes == 4) {
                    text_sec_ << "    movss xmm0, [rax]\n";
                } else {
                    text_sec_ << "    movsd xmm0, [rax]\n";
                }
            } else if (is_16byte_type(elem_ty)) {
                text_sec_ << "    mov rdx, [rax + 8]\n";
                text_sec_ << "    mov rax, [rax]\n";
            } else if (elem_ty && elem_ty->size_bytes == 8) {
                text_sec_ << "    mov rax, [rax]\n";
            } else if (elem_ty && elem_ty->size_bytes == 2) {
                text_sec_ << "    movzx eax, word [rax]\n";
            } else if (elem_ty && elem_ty->size_bytes == 1) {
                text_sec_ << "    movzx eax, byte [rax]\n";
            } else {
                text_sec_ << "    mov eax, [rax]\n";
            }
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
                } else if (is_128bit_expr(expr->left)) {
                    emit_expression(expr->left);
                    text_sec_ << "    not rax\n    not rdx\n    add rax, 1\n    adc rdx, 0\n";
                } else if (is_64bit_expr(expr->left)) {
                    emit_expression(expr->left);
                    text_sec_ << "    neg rax\n";
                } else {
                    emit_expression(expr->left);
                    text_sec_ << "    neg eax\n";
                }
            } else if (expr->op == "!") {
                emit_expression(expr->left);
                text_sec_ << "    test eax, eax\n    sete al\n    movzx eax, al\n";
            } else if (expr->op == "~") {
                if (is_128bit_expr(expr->left)) {
                    emit_expression(expr->left);
                    text_sec_ << "    not rax\n    not rdx\n";
                } else if (is_64bit_expr(expr->left)) {
                    emit_expression(expr->left);
                    text_sec_ << "    not rax\n";
                } else {
                    emit_expression(expr->left);
                    text_sec_ << "    not eax\n";
                }
            }
            break;
        }
        case ExprKind::PostfixUnwrap: {
            std::string ok_lbl = next_label("L_unwrap_ok");
            emit_expression(expr->left);
            text_sec_ << "    test rdx, rdx\n";
            text_sec_ << "    jz " << ok_lbl << "\n";
            bool has_defers = false;
            for (const auto& scope : defer_scopes_) {
                if (!scope.empty()) { has_defers = true; break; }
            }
            if (has_defers) {
                text_sec_ << "    push rdx\n    push rax\n";
                uint32_t dummy_off = current_stack_offset_;
                emit_deferred_statements(dummy_off);
                text_sec_ << "    pop rax\n    pop rdx\n";
            }
            if (target_os_ == TargetOS::Windows) {
                text_sec_ << "    mov rsp, rbp\n";
                text_sec_ << "    pop rdi\n";
                text_sec_ << "    pop rsi\n";
                text_sec_ << "    pop rbp\n";
                text_sec_ << "    ret\n";
            } else {
                text_sec_ << "    mov rsp, rbp\n    pop rbp\n    ret\n";
            }
            text_sec_ << ok_lbl << ":\n";
            break;
        }
        case ExprKind::Match: {
            std::string match_end_lbl = next_label("L_match_end");
            emit_expression(expr->left);

            current_stack_offset_ += 8;
            uint32_t subj_off = current_stack_offset_;
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

            // String equality comparisons
            if ((expr->op == "==" || expr->op == "!=") && (is_string_expr(expr->left) || is_string_expr(expr->right))) {
                if (target_os_ == TargetOS::Windows) {
                    emit_expression(expr->left);  text_sec_ << "    push rax\n";
                    emit_expression(expr->right); text_sec_ << "    mov rdx, rax\n    pop rcx\n";
                    text_sec_ << "    sub rsp, 32\n    call strcmp\n    add rsp, 32\n";
                } else {
                    emit_expression(expr->left);  text_sec_ << "    push rax\n";
                    emit_expression(expr->right); text_sec_ << "    mov rsi, rax\n    pop rdi\n";
                    text_sec_ << "    call strcmp\n";
                }
                if (expr->op == "==") {
                    text_sec_ << "    cmp eax, 0\n    sete al\n    movzx eax, al\n";
                } else {
                    text_sec_ << "    cmp eax, 0\n    setne al\n    movzx eax, al\n";
                }
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

            if (is_128bit_expr(expr->left) || is_128bit_expr(expr->right)) {
                emit_expression(expr->left);
                text_sec_ << "    push rdx\n    push rax\n";
                emit_expression(expr->right);
                text_sec_ << "    mov rbx, rax\n    mov rcx, rdx\n";
                text_sec_ << "    pop rax\n    pop rdx\n";

                if (expr->op == "+") {
                    text_sec_ << "    add rax, rbx\n    adc rdx, rcx\n";
                } else if (expr->op == "-") {
                    text_sec_ << "    sub rax, rbx\n    sbb rdx, rcx\n";
                } else if (expr->op == "&") {
                    text_sec_ << "    and rax, rbx\n    and rdx, rcx\n";
                } else if (expr->op == "|") {
                    text_sec_ << "    or rax, rbx\n    or rdx, rcx\n";
                } else if (expr->op == "^") {
                    text_sec_ << "    xor rax, rbx\n    xor rdx, rcx\n";
                } else if (expr->op == "==") {
                    text_sec_ << "    cmp rax, rbx\n    sete al\n";
                    text_sec_ << "    cmp rdx, rcx\n    sete bl\n";
                    text_sec_ << "    and al, bl\n    movzx eax, al\n";
                } else if (expr->op == "!=") {
                    text_sec_ << "    cmp rax, rbx\n    setne al\n";
                    text_sec_ << "    cmp rdx, rcx\n    setne bl\n";
                    text_sec_ << "    or al, bl\n    movzx eax, al\n";
                } else if (expr->op == "<") {
                    std::string lt_high = next_label("L_128_lt_high");
                    std::string lt_true = next_label("L_128_lt_true");
                    std::string lt_done = next_label("L_128_lt_done");
                    text_sec_ << "    cmp rdx, rcx\n    jne " << lt_high << "\n";
                    text_sec_ << "    cmp rax, rbx\n    jb " << lt_true << "\n";
                    text_sec_ << "    xor eax, eax\n    jmp " << lt_done << "\n";
                    text_sec_ << lt_high << ":\n    jl " << lt_true << "\n    xor eax, eax\n    jmp " << lt_done << "\n";
                    text_sec_ << lt_true << ":\n    mov eax, 1\n";
                    text_sec_ << lt_done << ":\n";
                } else if (expr->op == ">") {
                    std::string gt_high = next_label("L_128_gt_high");
                    std::string gt_true = next_label("L_128_gt_true");
                    std::string gt_done = next_label("L_128_gt_done");
                    text_sec_ << "    cmp rdx, rcx\n    jne " << gt_high << "\n";
                    text_sec_ << "    cmp rax, rbx\n    ja " << gt_true << "\n";
                    text_sec_ << "    xor eax, eax\n    jmp " << gt_done << "\n";
                    text_sec_ << gt_high << ":\n    jg " << gt_true << "\n    xor eax, eax\n    jmp " << gt_done << "\n";
                    text_sec_ << gt_true << ":\n    mov eax, 1\n";
                    text_sec_ << gt_done << ":\n";
                }
                return;
            }

            bool is_64 = is_64bit_expr(expr->left) || is_64bit_expr(expr->right);

            emit_expression(expr->left);  text_sec_ << "    push rax\n";
            emit_expression(expr->right); text_sec_ << "    mov rbx, rax\n    pop rax\n";

            if (is_64) {
                if (expr->op == "+") text_sec_ << "    add rax, rbx\n";
                else if (expr->op == "-") text_sec_ << "    sub rax, rbx\n";
                else if (expr->op == "*") text_sec_ << "    imul rax, rbx\n";
                else if (expr->op == "/") text_sec_ << "    cqo\n    idiv rbx\n";
                else if (expr->op == "%") text_sec_ << "    cqo\n    idiv rbx\n    mov rax, rdx\n";
                else if (expr->op == "&") text_sec_ << "    and rax, rbx\n";
                else if (expr->op == "|") text_sec_ << "    or rax, rbx\n";
                else if (expr->op == "^") text_sec_ << "    xor rax, rbx\n";
                else if (expr->op == "<<") text_sec_ << "    mov rcx, rbx\n    shl rax, cl\n";
                else if (expr->op == ">>") text_sec_ << "    mov rcx, rbx\n    sar rax, cl\n";
                else if (expr->op == "==") text_sec_ << "    cmp rax, rbx\n    sete al\n    movzx eax, al\n";
                else if (expr->op == "!=") text_sec_ << "    cmp rax, rbx\n    setne al\n    movzx eax, al\n";
                else if (expr->op == "<")  text_sec_ << "    cmp rax, rbx\n    setl al\n    movzx eax, al\n";
                else if (expr->op == "<=") text_sec_ << "    cmp rax, rbx\n    setle al\n    movzx eax, al\n";
                else if (expr->op == ">")  text_sec_ << "    cmp rax, rbx\n    setg al\n    movzx eax, al\n";
                else if (expr->op == ">=") text_sec_ << "    cmp rax, rbx\n    setge al\n    movzx eax, al\n";
            } else {
                if (expr->op == "+") text_sec_ << "    add eax, ebx\n";
                else if (expr->op == "-") text_sec_ << "    sub eax, ebx\n";
                else if (expr->op == "*") text_sec_ << "    imul eax, ebx\n";
                else if (expr->op == "/") text_sec_ << "    cdq\n    idiv ebx\n";
                else if (expr->op == "%") text_sec_ << "    cdq\n    idiv ebx\n    mov eax, edx\n";
                else if (expr->op == "&") text_sec_ << "    and eax, ebx\n";
                else if (expr->op == "|") text_sec_ << "    or eax, ebx\n";
                else if (expr->op == "^") text_sec_ << "    xor eax, ebx\n";
                else if (expr->op == "<<") text_sec_ << "    mov ecx, ebx\n    shl eax, cl\n";
                else if (expr->op == ">>") text_sec_ << "    mov ecx, ebx\n    sar eax, cl\n";
                else if (expr->op == "==") text_sec_ << "    cmp eax, ebx\n    sete al\n    movzx eax, al\n";
                else if (expr->op == "!=") text_sec_ << "    cmp eax, ebx\n    setne al\n    movzx eax, al\n";
                else if (expr->op == "<")  text_sec_ << "    cmp eax, ebx\n    setl al\n    movzx eax, al\n";
                else if (expr->op == "<=") text_sec_ << "    cmp eax, ebx\n    setle al\n    movzx eax, al\n";
                else if (expr->op == ">")  text_sec_ << "    cmp eax, ebx\n    setg al\n    movzx eax, al\n";
                else if (expr->op == ">=") text_sec_ << "    cmp eax, ebx\n    setge al\n    movzx eax, al\n";
            }
            break;
        }
        case ExprKind::Call: {
            const ASTFunctionDecl* target_fn = nullptr;
            std::string callee;
            if (expr->left && expr->left->kind == ExprKind::Identifier) {
                callee = std::string(expr->left->raw_text);
                if (!expr->generic_args.empty() && callee.find("__") == std::string::npos) {
                    for (auto* a : expr->generic_args) {
                        callee += "__" + TypeChecker::get_type_name(a);
                    }
                }
                if (current_program_) {
                    for (const auto* f : current_program_->functions) {
                        if (f->name == callee || f->name.ends_with("::" + callee)) {
                            target_fn = f;
                            if (f->is_extern_c) {
                                std::string fn_n = std::string(f->name);
                                auto last_colons = fn_n.rfind("::");
                                callee = (last_colons != std::string::npos) ? fn_n.substr(last_colons + 2) : fn_n;
                            } else {
                                callee = std::string(f->name);
                            }
                            break;
                        }
                    }
                }
            }

            // Native variadic slice call (any... args)
            if (target_fn && target_fn->has_variadic_slice && !target_fn->is_extern_c) {
                size_t fixed_count = target_fn->params.size() - 1;
                size_t var_count = expr->args.size() >= fixed_count ? expr->args.size() - fixed_count : 0;

                uint32_t any_arr_off = current_stack_offset_ + (uint32_t)(var_count * 16 + 16);
                any_arr_off = (any_arr_off + 15) & ~15;

                for (size_t j = 0; j < var_count; ++j) {
                    const auto* varg = expr->args[fixed_count + j];
                    uint64_t tid = get_type_id(varg);
                    emit_expression(varg);
                    if (is_float_expr(varg)) {
                        text_sec_ << "    movq rax, xmm0\n";
                    }
                    uint32_t elem_off = any_arr_off - (uint32_t)(j * 16);
                    text_sec_ << "    mov [rbp - " << elem_off << "], rax\n";
                    text_sec_ << "    mov qword [rbp - " << (elem_off - 8) << "], " << tid << "\n";
                }

                const char* int_arg_regs_sysv[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
                const char* int_arg_regs_ms64[] = { "rcx", "rdx", "r8", "r9" };
                size_t cur_int = 0;

                for (size_t i = 0; i < fixed_count; ++i) {
                    emit_expression(expr->args[i]);
                    if (target_os_ == TargetOS::Windows) {
                        if (cur_int < 4) text_sec_ << "    mov " << int_arg_regs_ms64[cur_int++] << ", rax\n";
                    } else {
                        if (cur_int < 6) text_sec_ << "    mov " << int_arg_regs_sysv[cur_int++] << ", rax\n";
                    }
                }

                if (target_os_ == TargetOS::Windows) {
                    if (cur_int + 1 < 4) {
                        if (var_count > 0) text_sec_ << "    lea " << int_arg_regs_ms64[cur_int++] << ", [rbp - " << any_arr_off << "]\n";
                        else text_sec_ << "    xor " << int_arg_regs_ms64[cur_int++] << ", " << int_arg_regs_ms64[cur_int] << "\n";
                        text_sec_ << "    mov " << int_arg_regs_ms64[cur_int++] << ", " << var_count << "\n";
                    }
                } else {
                    if (cur_int + 1 < 6) {
                        if (var_count > 0) text_sec_ << "    lea " << int_arg_regs_sysv[cur_int++] << ", [rbp - " << any_arr_off << "]\n";
                        else text_sec_ << "    xor " << int_arg_regs_sysv[cur_int++] << ", " << int_arg_regs_sysv[cur_int] << "\n";
                        text_sec_ << "    mov " << int_arg_regs_sysv[cur_int++] << ", " << var_count << "\n";
                    }
                }

                std::string call_lbl_aligned = next_label("L_call_aligned");
                std::string call_lbl_done    = next_label("L_call_done");

                if (target_os_ == TargetOS::Windows) {
                    text_sec_ << "    sub rsp, 32\n";
                    text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                    text_sec_ << "    add rsp, 32\n";
                } else {
                    text_sec_ << "    test rsp, 15\n";
                    text_sec_ << "    jz " << call_lbl_aligned << "\n";
                    text_sec_ << "    sub rsp, 8\n";
                    text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                    text_sec_ << "    add rsp, 8\n";
                    text_sec_ << "    jmp " << call_lbl_done << "\n";
                    text_sec_ << call_lbl_aligned << ":\n";
                    text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                    text_sec_ << call_lbl_done << ":\n";
                }
                return;
            }

            const char* int_arg_regs_sysv[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
            const char* int_arg_regs_ms64[] = { "rcx", "rdx", "r8", "r9" };

            std::vector<bool> is_float_arg;
            std::vector<bool> is_16b_arg;
            for (size_t i = 0; i < expr->args.size(); ++i) {
                is_float_arg.push_back(is_float_expr(expr->args[i]));
                is_16b_arg.push_back(is_16byte_expr(expr->args[i]));
            }

            if (target_os_ == TargetOS::Windows) {
                size_t num_args = expr->args.size();
                size_t extra_args = (num_args > 4) ? (num_args - 4) : 0;
                size_t call_stack = 32 + extra_args * 8;
                call_stack = (call_stack + 15) & ~15;

                text_sec_ << "    sub rsp, " << call_stack << "\n";

                // Pass arguments 5+ on stack at [rsp + 32 + (i - 4)*8]
                for (size_t i = 4; i < num_args; ++i) {
                    emit_expression(expr->args[i]);
                    if (is_float_arg[i]) {
                        text_sec_ << "    movsd [rsp + " << (32 + (i - 4) * 8) << "], xmm0\n";
                    } else {
                        text_sec_ << "    mov [rsp + " << (32 + (i - 4) * 8) << "], rax\n";
                    }
                }

                // Pass arguments 1-4 in registers (and float registers)
                for (int i = (int)std::min(num_args, (size_t)4) - 1; i >= 0; --i) {
                    emit_expression(expr->args[i]);
                    if (is_float_arg[i]) {
                        text_sec_ << "    movaps xmm" << i << ", xmm0\n";
                        text_sec_ << "    movq " << int_arg_regs_ms64[i] << ", xmm0\n";
                    } else {
                        text_sec_ << "    mov " << int_arg_regs_ms64[i] << ", rax\n";
                    }
                }

                text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                text_sec_ << "    add rsp, " << call_stack << "\n";
            } else {
                // System V (Linux)
                for (size_t i = 0; i < expr->args.size(); ++i) {
                    if (is_float_arg[i]) {
                        emit_expression(expr->args[i]);
                        text_sec_ << "    sub rsp, 8\n    movsd [rsp], xmm0\n";
                    } else if (is_16b_arg[i]) {
                        emit_expression(expr->args[i]);
                        text_sec_ << "    push rdx\n    push rax\n";
                    } else {
                        emit_expression(expr->args[i]);
                        text_sec_ << "    push rax\n";
                    }
                }

                int int_slots = 0;
                int flt_count = 0;
                for (size_t i = 0; i < expr->args.size(); ++i) {
                    if (is_float_arg[i]) flt_count++;
                    else if (is_16b_arg[i]) int_slots += 2;
                    else int_slots += 1;
                }

                int cur_flt = flt_count - 1;
                int cur_int_slot = int_slots - 1;

                for (int i = (int)expr->args.size() - 1; i >= 0; --i) {
                    if (is_float_arg[i]) {
                        if (cur_flt < 8) text_sec_ << "    movsd xmm" << cur_flt << ", [rsp]\n    add rsp, 8\n";
                        else text_sec_ << "    add rsp, 8\n";
                        cur_flt--;
                    } else if (is_16b_arg[i]) {
                        if (cur_int_slot - 1 < 6) text_sec_ << "    pop " << int_arg_regs_sysv[cur_int_slot - 1] << "\n";
                        else text_sec_ << "    add rsp, 8\n";
                        if (cur_int_slot < 6) text_sec_ << "    pop " << int_arg_regs_sysv[cur_int_slot] << "\n";
                        else text_sec_ << "    add rsp, 8\n";
                        cur_int_slot -= 2;
                    } else {
                        if (cur_int_slot < 6) text_sec_ << "    pop " << int_arg_regs_sysv[cur_int_slot] << "\n";
                        else text_sec_ << "    add rsp, 8\n";
                        cur_int_slot--;
                    }
                }

                if (flt_count > 0) {
                    text_sec_ << "    mov al, " << std::min(flt_count, 8) << "\n";
                }

                std::string call_lbl_aligned = next_label("L_call_aligned");
                std::string call_lbl_done    = next_label("L_call_done");

                text_sec_ << "    test rsp, 15\n";
                text_sec_ << "    jz " << call_lbl_aligned << "\n";
                text_sec_ << "    sub rsp, 8\n";
                text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                text_sec_ << "    add rsp, 8\n";
                text_sec_ << "    jmp " << call_lbl_done << "\n";
                text_sec_ << call_lbl_aligned << ":\n";
                text_sec_ << "    call " << sanitize_nasm_identifier(callee) << "\n";
                text_sec_ << call_lbl_done << ":\n";
            }
            break;
        }
        default: break;
    }
}

} // namespace femto