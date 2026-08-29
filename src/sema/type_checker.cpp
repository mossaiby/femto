#include "sema/type_checker.hpp"
#include <cstring>
#include <algorithm>

namespace femto {

std::string TypeChecker::get_type_name(const ASTType* ty) {
    if (!ty) return "int32";
    if (ty->kind == TypeKind::Primitive) {
        switch (ty->primitive_kind) {
            case TokenKind::KwInt8: return "int8";
            case TokenKind::KwInt16: return "int16";
            case TokenKind::KwInt32: return "int32";
            case TokenKind::KwInt64: return "int64";
            case TokenKind::KwUint8: return "uint8";
            case TokenKind::KwUint16: return "uint16";
            case TokenKind::KwUint32: return "uint32";
            case TokenKind::KwUint64: return "uint64";
            case TokenKind::KwFloat16: return "float16";
            case TokenKind::KwFloat32: return "float32";
            case TokenKind::KwFloat64: return "float64";
            case TokenKind::KwFloat128: return "float128";
            case TokenKind::KwBool8: return "bool8";
            case TokenKind::KwChar8: return "char8";
            case TokenKind::KwString8: return "string8";
            case TokenKind::KwString16: return "string16";
            case TokenKind::KwString32: return "string32";
            case TokenKind::KwVoid: return "void";
            default: return "int32";
        }
    }
    if (ty->kind == TypeKind::Custom) {
        std::string n = std::string(ty->custom_name);
        for (auto* g : ty->generic_args) {
            n += "__" + get_type_name(g);
        }
        return n;
    }
    if (ty->kind == TypeKind::Pointer) {
        return get_type_name(ty->pointee_or_element) + "_ptr";
    }
    if (ty->kind == TypeKind::Slice) {
        return get_type_name(ty->pointee_or_element) + "_slice";
    }
    if (ty->kind == TypeKind::Array) {
        return get_type_name(ty->pointee_or_element) + "_arr" + std::to_string(ty->array_size);
    }
    return "int32";
}

void TypeChecker::init_primitives() {
    auto make_prim = [&](TokenKind k, uint32_t sz) {
        auto* t = new SemaType{SemaType::Kind::Primitive, sz, sz, PrimitiveTypeInfo{k}};
        return t;
    };

    type_env_["int8"]     = make_prim(TokenKind::KwInt8, 1);
    type_env_["int16"]    = make_prim(TokenKind::KwInt16, 2);
    type_env_["int32"]    = make_prim(TokenKind::KwInt32, 4);
    type_env_["int64"]    = make_prim(TokenKind::KwInt64, 8);
    type_env_["uint8"]    = make_prim(TokenKind::KwUint8, 1);
    type_env_["uint16"]   = make_prim(TokenKind::KwUint16, 2);
    type_env_["uint32"]   = make_prim(TokenKind::KwUint32, 4);
    type_env_["uint64"]   = make_prim(TokenKind::KwUint64, 8);
    type_env_["float16"]  = make_prim(TokenKind::KwFloat16, 2);
    type_env_["float32"]  = make_prim(TokenKind::KwFloat32, 4);
    type_env_["float64"]  = make_prim(TokenKind::KwFloat64, 8);
    type_env_["float128"] = make_prim(TokenKind::KwFloat128, 16);
    type_env_["bool8"]    = make_prim(TokenKind::KwBool8, 1);
    type_env_["char8"]    = make_prim(TokenKind::KwChar8, 1);
    type_env_["string8"]  = make_prim(TokenKind::KwString8, 8);
    type_env_["string16"] = make_prim(TokenKind::KwString16, 8);
    type_env_["string32"] = make_prim(TokenKind::KwString32, 8);
    type_env_["void"]     = make_prim(TokenKind::KwVoid, 0);
}

void TypeChecker::compute_struct_layout(ASTStructDecl* decl, std::string custom_name) {
    StructTypeInfo info;
    info.name = custom_name.empty() ? std::string(decl->name) : custom_name;

    uint32_t offset = 0;
    uint32_t max_align = 1;

    for (auto& f : decl->fields) {
        auto* f_type = resolve_ast_type(f.type);
        if (!f_type) f_type = type_env_["int32"];

        uint32_t align = std::max(1u, f_type->align_bytes);
        offset = (offset + align - 1) & ~(align - 1);

        StructFieldInfo fi;
        fi.name = std::string(f.name);
        fi.type = f_type;
        fi.offset = offset;

        info.field_map[fi.name] = info.fields.size();
        info.fields.push_back(fi);

        offset += f_type->size_bytes;
        max_align = std::max(max_align, align);
    }

    uint32_t total_size = (offset + max_align - 1) & ~(max_align - 1);
    if (total_size == 0) total_size = 1;

    auto* st = new SemaType{SemaType::Kind::Struct, total_size, max_align, info};
    type_env_[info.name] = st;
}

void TypeChecker::compute_union_layout(ASTUnionDecl* decl, std::string custom_name) {
    UnionTypeInfo info;
    info.name = custom_name.empty() ? std::string(decl->name) : custom_name;

    uint32_t max_size = 0;
    uint32_t max_align = 1;

    for (auto& f : decl->fields) {
        auto* f_type = resolve_ast_type(f.type);
        if (!f_type) f_type = type_env_["int32"];

        uint32_t align = std::max(1u, f_type->align_bytes);
        uint32_t sz = std::max(1u, f_type->size_bytes);

        UnionFieldInfo fi;
        fi.name = std::string(f.name);
        fi.type = f_type;
        fi.offset = 0;

        info.field_map[fi.name] = info.fields.size();
        info.fields.push_back(fi);

        max_size = std::max(max_size, sz);
        max_align = std::max(max_align, align);
    }

    uint32_t total_size = (max_size + max_align - 1) & ~(max_align - 1);
    if (total_size == 0) total_size = 1;

    auto* un = new SemaType{SemaType::Kind::Union, total_size, max_align, info};
    type_env_[info.name] = un;
}

std::string TypeChecker::monomorphize_struct(ASTType* generic_ty) {
    std::string mangled = std::string(generic_ty->custom_name);
    for (auto* a : generic_ty->generic_args) {
        mangled += "__" + get_type_name(a);
    }

    if (type_env_.find(mangled) != type_env_.end()) {
        return mangled;
    }

    auto it = generic_structs_.find(std::string(generic_ty->custom_name));
    if (it == generic_structs_.end()) return mangled;

    ASTStructDecl* orig = it->second;
    std::unordered_map<std::string, ASTType*> subst;
    for (size_t i = 0; i < orig->generic_params.size() && i < generic_ty->generic_args.size(); ++i) {
        subst[std::string(orig->generic_params[i])] = generic_ty->generic_args[i];
    }

    auto* concrete_decl = arena_.allocate<ASTStructDecl>();
    concrete_decl->name = generic_ty->custom_name;
    for (auto& f : orig->fields) {
        ASTStructField cf;
        cf.name = f.name;
        cf.type = clone_and_substitute_type(f.type, subst);
        cf.default_value = f.default_value;
        concrete_decl->fields.push_back(cf);
    }

    compute_struct_layout(concrete_decl, mangled);
    return mangled;
}

std::string TypeChecker::monomorphize_union(ASTType* generic_ty) {
    std::string mangled = std::string(generic_ty->custom_name);
    for (auto* a : generic_ty->generic_args) {
        mangled += "__" + get_type_name(a);
    }

    if (type_env_.find(mangled) != type_env_.end()) {
        return mangled;
    }

    auto it = generic_unions_.find(std::string(generic_ty->custom_name));
    if (it == generic_unions_.end()) return mangled;

    ASTUnionDecl* orig = it->second;
    std::unordered_map<std::string, ASTType*> subst;
    for (size_t i = 0; i < orig->generic_params.size() && i < generic_ty->generic_args.size(); ++i) {
        subst[std::string(orig->generic_params[i])] = generic_ty->generic_args[i];
    }

    auto* concrete_decl = arena_.allocate<ASTUnionDecl>();
    concrete_decl->name = generic_ty->custom_name;
    for (auto& f : orig->fields) {
        ASTUnionField cf;
        cf.name = f.name;
        cf.type = clone_and_substitute_type(f.type, subst);
        cf.default_value = f.default_value;
        concrete_decl->fields.push_back(cf);
    }

    compute_union_layout(concrete_decl, mangled);
    return mangled;
}

std::string TypeChecker::monomorphize_function(ASTExpr* call_expr, ASTProgram& prog) {
    std::string orig_name = std::string(call_expr->left->raw_text);
    std::string mangled = orig_name;
    for (auto* a : call_expr->generic_args) {
        mangled += "__" + get_type_name(a);
    }

    for (auto* fn : prog.functions) {
        if (fn->name == mangled) {
            call_expr->left->raw_text = fn->name;
            return mangled;
        }
    }

    auto it = generic_functions_.find(orig_name);
    if (it == generic_functions_.end()) return orig_name;

    ASTFunctionDecl* orig_fn = it->second;
    std::unordered_map<std::string, ASTType*> subst;
    for (size_t i = 0; i < orig_fn->generic_params.size() && i < call_expr->generic_args.size(); ++i) {
        subst[std::string(orig_fn->generic_params[i])] = call_expr->generic_args[i];
    }

    char* m_buf = (char*)arena_.allocate_bytes(mangled.size() + 1, 1);
    std::memcpy(m_buf, mangled.data(), mangled.size());
    m_buf[mangled.size()] = '\0';
    std::string_view mangled_sv(m_buf, mangled.size());

    auto* concrete_fn = arena_.allocate<ASTFunctionDecl>();
    concrete_fn->name = mangled_sv;
    concrete_fn->is_exported = orig_fn->is_exported;
    concrete_fn->return_type = clone_and_substitute_type(orig_fn->return_type, subst);

    for (auto& p : orig_fn->params) {
        ASTParam cp;
        cp.name = p.name;
        cp.type = clone_and_substitute_type(p.type, subst);
        concrete_fn->params.push_back(cp);
    }

    for (auto* s : orig_fn->body) {
        concrete_fn->body.push_back(clone_and_substitute_stmt(s, subst));
    }

    prog.functions.push_back(concrete_fn);
    call_expr->left->raw_text = mangled_sv;
    return mangled;
}

ASTType* TypeChecker::clone_and_substitute_type(ASTType* ty, const std::unordered_map<std::string, ASTType*>& subst) {
    if (!ty) return nullptr;
    if (ty->kind == TypeKind::Custom) {
        auto it = subst.find(std::string(ty->custom_name));
        if (it != subst.end()) {
            return it->second;
        }
    }
    auto* ct = arena_.allocate<ASTType>();
    *ct = *ty;
    if (ty->pointee_or_element) {
        ct->pointee_or_element = clone_and_substitute_type(ty->pointee_or_element, subst);
    }
    ct->generic_args.clear();
    for (auto* a : ty->generic_args) {
        ct->generic_args.push_back(clone_and_substitute_type(a, subst));
    }
    return ct;
}

ASTExpr* TypeChecker::clone_and_substitute_expr(ASTExpr* expr, const std::unordered_map<std::string, ASTType*>& subst) {
    if (!expr) return nullptr;
    auto* ce = arena_.allocate<ASTExpr>();
    *ce = *expr;
    ce->left = clone_and_substitute_expr(expr->left, subst);
    ce->right = clone_and_substitute_expr(expr->right, subst);
    if (expr->target_type) {
        ce->target_type = clone_and_substitute_type(expr->target_type, subst);
    }
    ce->args.clear();
    for (auto* a : expr->args) ce->args.push_back(clone_and_substitute_expr(a, subst));
    return ce;
}

ASTStmt* TypeChecker::clone_and_substitute_stmt(ASTStmt* stmt, const std::unordered_map<std::string, ASTType*>& subst) {
    if (!stmt) return nullptr;
    auto* cs = arena_.allocate<ASTStmt>();
    *cs = *stmt;
    if (stmt->type_annot) cs->type_annot = clone_and_substitute_type(stmt->type_annot, subst);
    cs->init_expr = clone_and_substitute_expr(stmt->init_expr, subst);
    cs->init_stmt = clone_and_substitute_stmt(stmt->init_stmt, subst);
    cs->step_stmt = clone_and_substitute_stmt(stmt->step_stmt, subst);
    cs->target_expr = clone_and_substitute_expr(stmt->target_expr, subst);
    cs->value_expr = clone_and_substitute_expr(stmt->value_expr, subst);
    cs->condition = clone_and_substitute_expr(stmt->condition, subst);
    cs->then_block.clear();
    for (auto* s : stmt->then_block) cs->then_block.push_back(clone_and_substitute_stmt(s, subst));
    cs->else_block.clear();
    for (auto* s : stmt->else_block) cs->else_block.push_back(clone_and_substitute_stmt(s, subst));
    return cs;
}

static int64_t parse_literal_int(std::string_view raw) {
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

int64_t TypeChecker::eval_const_expr(ASTExpr* expr) {
    if (!expr) return 0;
    if (expr->kind == ExprKind::Literal) {
        return parse_literal_int(expr->raw_text);
    }
    if (expr->kind == ExprKind::Identifier) {
        auto it = const_defs_.find(std::string(expr->raw_text));
        if (it != const_defs_.end()) return it->second;
    }
    if (expr->kind == ExprKind::BuiltinSizeof && expr->target_type) {
        auto* ty = resolve_ast_type(expr->target_type);
        return ty ? ty->size_bytes : 4;
    }
    if (expr->kind == ExprKind::BuiltinAlignof && expr->target_type) {
        auto* ty = resolve_ast_type(expr->target_type);
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

SemaType* TypeChecker::resolve_ast_type(ASTType* ast_ty) {
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
        if (!ast_ty->generic_args.empty()) {
            std::string mangled = monomorphize_struct(ast_ty);
            auto it = type_env_.find(mangled);
            if (it != type_env_.end()) return it->second;

            std::string mangled_u = monomorphize_union(ast_ty);
            auto u_it = type_env_.find(mangled_u);
            if (u_it != type_env_.end()) return u_it->second;
        }
        auto it = type_env_.find(std::string(ast_ty->custom_name));
        if (it != type_env_.end()) return it->second;
    }
    if (ast_ty->kind == TypeKind::Pointer) {
        auto* pointee = resolve_ast_type(ast_ty->pointee_or_element);
        auto* pt = new SemaType{SemaType::Kind::Pointer, 8, 8, PointerTypeInfo{pointee}};
        return pt;
    }
    if (ast_ty->kind == TypeKind::Array) {
        auto* elem = resolve_ast_type(ast_ty->pointee_or_element);
        uint32_t sz = elem ? (uint32_t)(elem->size_bytes * ast_ty->array_size) : 0;
        uint32_t al = elem ? elem->align_bytes : 4;
        auto* arr = new SemaType{SemaType::Kind::Array, sz, al, ArrayTypeInfo{elem, ast_ty->array_size}};
        return arr;
    }
    if (ast_ty->kind == TypeKind::Slice) {
        auto* elem = resolve_ast_type(ast_ty->pointee_or_element);
        auto* s = new SemaType{SemaType::Kind::Slice, 16, 8, SliceTypeInfo{elem}};
        return s;
    }
    if (ast_ty->kind == TypeKind::Result) {
        auto* payload = resolve_ast_type(ast_ty->pointee_or_element);
        uint32_t sz = 16;
        auto* r = new SemaType{SemaType::Kind::Result, sz, 8, ResultTypeInfo{payload}};
        return r;
    }
    return type_env_["int32"];
}

bool TypeChecker::check_type_compatibility(SemaType* expected, SemaType* actual, SourceSpan span) {
    if (!expected || !actual) return true;
    if (expected == actual) return true;
    diag_.report_error(span, "type mismatch: cannot implicitly convert between distinct types");
    return false;
}

void TypeChecker::check_statement(ASTStmt* stmt, SemaType* return_type, ASTProgram& prog) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::VarDecl: {
            auto* declared_t = resolve_ast_type(stmt->type_annot);
            symbol_table_[std::string(stmt->name)] = declared_t;
            if (stmt->init_expr) {
                check_expression(stmt->init_expr, prog);
            }
            break;
        }
        case StmtKind::Assignment:
        case StmtKind::CompoundAssignment: {
            if (stmt->target_expr) {
                check_expression(stmt->target_expr, prog);
            }
            if (stmt->value_expr) {
                check_expression(stmt->value_expr, prog);
            }
            break;
        }
        case StmtKind::Increment:
        case StmtKind::Decrement: {
            if (stmt->target_expr) {
                check_expression(stmt->target_expr, prog);
            }
            break;
        }
        case StmtKind::If: {
            check_expression(stmt->condition, prog);
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            for (auto* s : stmt->else_block) check_statement(s, return_type, prog);
            break;
        }
        case StmtKind::HashIf: {
            int64_t cond_val = eval_const_expr(stmt->condition);
            if (cond_val != 0) {
                for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            } else {
                for (auto* s : stmt->else_block) check_statement(s, return_type, prog);
            }
            break;
        }
        case StmtKind::While:
        case StmtKind::DoWhile: {
            current_loop_depth_++;
            check_expression(stmt->condition, prog);
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            current_loop_depth_--;
            break;
        }
        case StmtKind::For: {
            current_loop_depth_++;
            if (stmt->init_stmt) check_statement(stmt->init_stmt, return_type, prog);
            if (stmt->condition) check_expression(stmt->condition, prog);
            if (stmt->step_stmt) check_statement(stmt->step_stmt, return_type, prog);
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            current_loop_depth_--;
            break;
        }
        case StmtKind::Switch: {
            current_loop_depth_++;
            check_expression(stmt->condition, prog);
            for (auto& sc : stmt->switch_cases) {
                if (sc.match_val) check_expression(sc.match_val, prog);
                for (auto* s : sc.body) check_statement(s, return_type, prog);
            }
            current_loop_depth_--;
            break;
        }
        case StmtKind::Foreach: {
            current_loop_depth_++;
            check_expression(stmt->iter_collection, prog);
            auto* item_t = resolve_ast_type(stmt->iter_type);
            symbol_table_[std::string(stmt->iter_var)] = item_t;
            if (!stmt->iter_idx.empty()) {
                symbol_table_[std::string(stmt->iter_idx)] = type_env_["int32"];
            }
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            current_loop_depth_--;
            break;
        }
        case StmtKind::ResultBranch: {
            check_expression(stmt->condition, prog);
            symbol_table_[std::string(stmt->success_var)] = type_env_["int32"];
            for (auto* s : stmt->success_block) check_statement(s, return_type, prog);
            symbol_table_[std::string(stmt->failure_var)] = type_env_["int32"];
            for (auto* s : stmt->failure_block) check_statement(s, return_type, prog);
            break;
        }
        case StmtKind::Break:
        case StmtKind::Continue: {
            if (current_loop_depth_ == 0) {
                diag_.report_error(stmt->span, "break/continue outside of loop or switch");
            } else if (stmt->loop_levels > current_loop_depth_) {
                diag_.report_error(stmt->span, "break/continue level exceeds enclosing loop depth");
            }
            break;
        }
        case StmtKind::Return: {
            if (stmt->value_expr) {
                check_expression(stmt->value_expr, prog);
            }
            break;
        }
        case StmtKind::ExprStmt: {
            if (stmt->value_expr) {
                check_expression(stmt->value_expr, prog);
            }
            break;
        }
        default:
            break;
    }
}

SemaType* TypeChecker::check_expression(ASTExpr* expr, ASTProgram& prog) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case ExprKind::Literal: {
            if (expr->raw_text.front() == '"' || expr->raw_text.front() == '`') {
                return type_env_["string8"];
            }
            if (expr->raw_text.front() == '\'') {
                return type_env_["char8"];
            }
            if (expr->raw_text.find('.') != std::string_view::npos ||
                expr->raw_text.find('e') != std::string_view::npos ||
                expr->raw_text.find('E') != std::string_view::npos) {
                return type_env_["float64"];
            }
            return type_env_["int32"];
        }
        case ExprKind::Subject:
            return type_env_["int32"];
        case ExprKind::BuiltinSizeof:
        case ExprKind::BuiltinAlignof:
            return type_env_["int32"];
        case ExprKind::Cast: {
            check_expression(expr->left, prog);
            return resolve_ast_type(expr->target_type);
        }
        case ExprKind::BuiltinBitcast: {
            check_expression(expr->left, prog);
            return resolve_ast_type(expr->target_type);
        }
        case ExprKind::Identifier: {
            auto sep = expr->raw_text.find("::");
            if (sep != std::string_view::npos) {
                std::string enum_name(expr->raw_text.substr(0, sep));
                std::string var_name(expr->raw_text.substr(sep + 2));
                auto e_it = enum_defs_.find(enum_name);
                if (e_it != enum_defs_.end()) {
                    return type_env_["int32"];
                }
            }
            auto c_it = const_defs_.find(std::string(expr->raw_text));
            if (c_it != const_defs_.end()) {
                return type_env_["int32"];
            }
            auto it = symbol_table_.find(std::string(expr->raw_text));
            if (it != symbol_table_.end()) return it->second;
            return type_env_["int32"];
        }
        case ExprKind::Unary: {
            auto* sub = check_expression(expr->left, prog);
            if (expr->op == "&" && sub) {
                auto* pt = new SemaType{SemaType::Kind::Pointer, 8, 8, PointerTypeInfo{sub}};
                return pt;
            }
            if (expr->op == "*" && sub && sub->kind == SemaType::Kind::Pointer) {
                return std::get<PointerTypeInfo>(sub->data).pointee;
            }
            if (expr->op == "success") {
                auto* r = new SemaType{SemaType::Kind::Result, 16, 8, ResultTypeInfo{sub}};
                return r;
            }
            if (expr->op == "failure") {
                auto* r = new SemaType{SemaType::Kind::Result, 16, 8, ResultTypeInfo{type_env_["int32"]}};
                return r;
            }
            return sub;
        }
        case ExprKind::PostfixUnwrap: {
            auto* sub = check_expression(expr->left, prog);
            if (sub && sub->kind == SemaType::Kind::Result) {
                return std::get<ResultTypeInfo>(sub->data).payload;
            }
            return type_env_["int32"];
        }
        case ExprKind::Match: {
            check_expression(expr->left, prog);
            for (auto& arm : expr->match_arms) {
                if (arm.condition) check_expression(arm.condition, prog);
                for (auto* s : arm.statements) check_statement(s, nullptr, prog);
                if (arm.result_expr) check_expression(arm.result_expr, prog);
            }
            return type_env_["int32"];
        }
        case ExprKind::MemberAccess: {
            auto* base_t = check_expression(expr->left, prog);
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
                if (base_t && base_t->kind == SemaType::Kind::Slice && expr->raw_text == "length") {
                    return type_env_["int32"];
                }
            }
            return type_env_["int32"];
        }
        case ExprKind::Index: {
            auto* base_t = check_expression(expr->left, prog);
            check_expression(expr->right, prog);
            if (base_t) {
                if (base_t->kind == SemaType::Kind::Array) {
                    return std::get<ArrayTypeInfo>(base_t->data).element;
                }
                if (base_t->kind == SemaType::Kind::Slice) {
                    return std::get<SliceTypeInfo>(base_t->data).element;
                }
                if (base_t->kind == SemaType::Kind::Pointer) {
                    return std::get<PointerTypeInfo>(base_t->data).pointee;
                }
            }
            return type_env_["int32"];
        }
        case ExprKind::SliceSubrange: {
            auto* base_t = check_expression(expr->left, prog);
            for (auto* a : expr->args) check_expression(a, prog);
            if (base_t && base_t->kind == SemaType::Kind::Array) {
                auto* elem = std::get<ArrayTypeInfo>(base_t->data).element;
                auto* slice_t = new SemaType{SemaType::Kind::Slice, 16, 8, SliceTypeInfo{elem}};
                return slice_t;
            }
            return base_t;
        }
        case ExprKind::StructLiteral: {
            for (auto& sf : expr->struct_fields) {
                check_expression(sf.second, prog);
            }
            return type_env_["int32"];
        }
        case ExprKind::ArrayLiteral: {
            SemaType* elem_t = nullptr;
            for (auto* a : expr->args) {
                elem_t = check_expression(a, prog);
            }
            if (!elem_t) elem_t = type_env_["int32"];
            auto* arr_t = new SemaType{SemaType::Kind::Array, (uint32_t)(elem_t->size_bytes * expr->args.size()), elem_t->align_bytes, ArrayTypeInfo{elem_t, expr->args.size()}};
            return arr_t;
        }
        case ExprKind::Binary: {
            auto* lt = check_expression(expr->left, prog);
            auto* rt = check_expression(expr->right, prog);
            if ((lt && lt->is_floating_point()) || (rt && rt->is_floating_point())) {
                if (expr->op == "==" || expr->op == "!=" || expr->op == "<" ||
                    expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
                    return type_env_["int32"];
                }
                if (expr->op == "+" || expr->op == "-" || expr->op == "*" || expr->op == "/") {
                    if (lt && lt->size_bytes == 4 && rt && rt->size_bytes == 4) {
                        return type_env_["float32"];
                    }
                    return type_env_["float64"];
                }
                diag_.report_error(expr->span, "invalid binary operator for floating-point operands");
                return type_env_["float64"];
            }
            return type_env_["int32"];
        }
        case ExprKind::Call: {
            if (!expr->generic_args.empty() && expr->left && expr->left->kind == ExprKind::Identifier) {
                monomorphize_function(expr, prog);
            }
            for (auto* a : expr->args) check_expression(a, prog);

            if (expr->left && expr->left->kind == ExprKind::Identifier) {
                std::string callee = std::string(expr->left->raw_text);
                for (const auto* fn : prog.functions) {
                    if (fn->name == callee) {
                        return resolve_ast_type(fn->return_type);
                    }
                }
            }
            return type_env_["int32"];
        }
        default:
            return type_env_["int32"];
    }
}

bool TypeChecker::check_program(ASTProgram& prog) {
    init_primitives();

    // 1. Constants
    for (auto* c : prog.constants) {
        const_defs_[std::string(c->name)] = eval_const_expr(c->init_expr);
    }

    // 2. Enums
    for (auto* e : prog.enums) {
        for (auto& v : e->variants) {
            enum_defs_[std::string(e->name)][std::string(v.name)] = v.value.value_or(0);
        }
    }

    // 3. Collect Structs (Generic vs Concrete)
    for (auto* st : prog.structs) {
        if (!st->generic_params.empty()) {
            generic_structs_[std::string(st->name)] = st;
        } else {
            compute_struct_layout(st);
        }
    }

    // 4. Collect Unions (Generic vs Concrete)
    for (auto* un : prog.unions) {
        if (!un->generic_params.empty()) {
            generic_unions_[std::string(un->name)] = un;
        } else {
            compute_union_layout(un);
        }
    }

    // 5. Collect Generic Functions
    for (auto* fn : prog.functions) {
        if (!fn->generic_params.empty()) {
            generic_functions_[std::string(fn->name)] = fn;
        }
    }

    // 6. Typecheck Functions
    size_t i = 0;
    while (i < prog.functions.size()) {
        auto* fn = prog.functions[i];
        if (fn->generic_params.empty() && !fn->is_extern_c) {
            symbol_table_.clear();
            current_loop_depth_ = 0;
            for (auto& p : fn->params) {
                symbol_table_[std::string(p.name)] = resolve_ast_type(p.type);
            }
            auto* ret_type = resolve_ast_type(fn->return_type);
            for (auto* stmt : fn->body) {
                check_statement(stmt, ret_type, prog);
            }
        }
        i++;
    }

    return !diag_.has_errors();
}

} // namespace femto