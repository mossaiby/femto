#include "sema/type_checker.hpp"
#include <cstring>
#include <algorithm>

namespace femto {

static std::string eval_const_string_val(const ASTExpr* expr) {
    if (!expr) return "";
    if (expr->kind == ExprKind::Literal) {
        if (expr->raw_text.size() >= 2 && (expr->raw_text.front() == '"' || expr->raw_text.front() == '`')) {
            return std::string(expr->raw_text.substr(1, expr->raw_text.size() - 2));
        }
        return "";
    }
    if (expr->kind == ExprKind::BuiltinTarget) {
#ifdef _WIN32
        return "x86_64-windows";
#else
        return "x86_64-linux";
#endif
    }
    if (expr->kind == ExprKind::BuiltinArch) return "x86_64";
    if (expr->kind == ExprKind::BuiltinEndian) return "little";
    return "";
}

static bool types_are_equal_or_compatible(const SemaType* expected, const SemaType* actual, const std::unordered_map<std::string, SemaType*>& type_env) {
    if (!expected || !actual) return true;
    if (expected == actual) return true;

    auto any_it = type_env.find("any");
    auto* any_t = any_it != type_env.end() ? any_it->second : nullptr;
    if (expected == any_t) return true;

    if (expected->is_integer() && actual->is_integer()) {
        return true;
    }

    if (expected->is_floating_point() && actual->is_floating_point()) {
        return true;
    }

    if (expected->kind == SemaType::Kind::Pointer && actual->kind == SemaType::Kind::Pointer) {
        auto* exp_p = std::get<PointerTypeInfo>(expected->data).pointee;
        auto* act_p = std::get<PointerTypeInfo>(actual->data).pointee;
        if (!exp_p || !act_p) return true;
        auto void_it = type_env.find("void");
        auto* void_t = void_it != type_env.end() ? void_it->second : nullptr;
        if (exp_p == void_t || act_p == void_t) return true;
        return types_are_equal_or_compatible(exp_p, act_p, type_env);
    }

    if (expected->kind == SemaType::Kind::Slice && actual->kind == SemaType::Kind::Slice) {
        auto* exp_e = std::get<SliceTypeInfo>(expected->data).element;
        auto* act_e = std::get<SliceTypeInfo>(actual->data).element;
        return types_are_equal_or_compatible(exp_e, act_e, type_env);
    }

    if (expected->kind == SemaType::Kind::Array && actual->kind == SemaType::Kind::Array) {
        auto& exp_a = std::get<ArrayTypeInfo>(expected->data);
        auto& act_a = std::get<ArrayTypeInfo>(actual->data);
        if (exp_a.size != act_a.size) return false;
        return types_are_equal_or_compatible(exp_a.element, act_a.element, type_env);
    }

    if (expected->kind == SemaType::Kind::Result && actual->kind == SemaType::Kind::Result) {
        auto* exp_r = std::get<ResultTypeInfo>(expected->data).payload;
        auto* act_r = std::get<ResultTypeInfo>(actual->data).payload;
        return types_are_equal_or_compatible(exp_r, act_r, type_env);
    }

    if (expected->kind == SemaType::Kind::Struct && actual->kind == SemaType::Kind::Struct) {
        auto& exp_s = std::get<StructTypeInfo>(expected->data);
        auto& act_s = std::get<StructTypeInfo>(actual->data);
        return exp_s.name == act_s.name;
    }

    if (expected->kind == SemaType::Kind::Union && actual->kind == SemaType::Kind::Union) {
        auto& exp_u = std::get<UnionTypeInfo>(expected->data);
        auto& act_u = std::get<UnionTypeInfo>(actual->data);
        return exp_u.name == act_u.name;
    }

    return false;
}

std::string TypeChecker::get_type_name(const ASTType* ty) {
    if (!ty) return "int32";
    if (ty->kind == TypeKind::Primitive) {
        switch (ty->primitive_kind) {
            case TokenKind::KwAny: return "any";
            case TokenKind::KwInt8: return "int8";
            case TokenKind::KwInt16: return "int16";
            case TokenKind::KwInt32: return "int32";
            case TokenKind::KwInt64: return "int64";
            case TokenKind::KwInt128: return "int128";
            case TokenKind::KwUint8: return "uint8";
            case TokenKind::KwUint16: return "uint16";
            case TokenKind::KwUint32: return "uint32";
            case TokenKind::KwUint64: return "uint64";
            case TokenKind::KwUint128: return "uint128";
            case TokenKind::KwFloat16: return "float16";
            case TokenKind::KwFloat32: return "float32";
            case TokenKind::KwFloat64: return "float64";
            case TokenKind::KwFloat128: return "float128";
            case TokenKind::KwBool8: return "bool8";
            case TokenKind::KwChar8: return "char8";
            case TokenKind::KwChar16: return "char16";
            case TokenKind::KwChar32: return "char32";
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

std::string TypeChecker::get_type_name(const SemaType* ty) {
    if (!ty) return "int32";
    switch (ty->kind) {
        case SemaType::Kind::Primitive: {
            auto pk = std::get<PrimitiveTypeInfo>(ty->data).kind;
            switch (pk) {
                case TokenKind::KwAny: return "any";
                case TokenKind::KwInt8: return "int8";
                case TokenKind::KwInt16: return "int16";
                case TokenKind::KwInt32: return "int32";
                case TokenKind::KwInt64: return "int64";
                case TokenKind::KwInt128: return "int128";
                case TokenKind::KwInt256: return "int256";
                case TokenKind::KwInt512: return "int512";
                case TokenKind::KwUint8: return "uint8";
                case TokenKind::KwUint16: return "uint16";
                case TokenKind::KwUint32: return "uint32";
                case TokenKind::KwUint64: return "uint64";
                case TokenKind::KwUint128: return "uint128";
                case TokenKind::KwUint256: return "uint256";
                case TokenKind::KwUint512: return "uint512";
                case TokenKind::KwFloat16: return "float16";
                case TokenKind::KwFloat32: return "float32";
                case TokenKind::KwFloat64: return "float64";
                case TokenKind::KwFloat128: return "float128";
                case TokenKind::KwBool8: return "bool8";
                case TokenKind::KwBool16: return "bool16";
                case TokenKind::KwBool32: return "bool32";
                case TokenKind::KwBool64: return "bool64";
                case TokenKind::KwBool128: return "bool128";
                case TokenKind::KwBool256: return "bool256";
                case TokenKind::KwBool512: return "bool512";
                case TokenKind::KwChar8: return "char8";
                case TokenKind::KwChar16: return "char16";
                case TokenKind::KwChar32: return "char32";
                case TokenKind::KwString8: return "string8";
                case TokenKind::KwString16: return "string16";
                case TokenKind::KwString32: return "string32";
                case TokenKind::KwVoid: return "void";
                default: return "int32";
            }
        }
        case SemaType::Kind::Pointer: {
            auto* p = std::get<PointerTypeInfo>(ty->data).pointee;
            return get_type_name(p) + "*";
        }
        case SemaType::Kind::Array: {
            auto& a = std::get<ArrayTypeInfo>(ty->data);
            return get_type_name(a.element) + "[" + std::to_string(a.size) + "]";
        }
        case SemaType::Kind::Slice: {
            auto* s = std::get<SliceTypeInfo>(ty->data).element;
            return get_type_name(s) + "[]";
        }
        case SemaType::Kind::Result: {
            auto* r = std::get<ResultTypeInfo>(ty->data).payload;
            return "!" + (r ? get_type_name(r) : "void");
        }
        case SemaType::Kind::Struct: {
            return std::get<StructTypeInfo>(ty->data).name;
        }
        case SemaType::Kind::Union: {
            return std::get<UnionTypeInfo>(ty->data).name;
        }
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
    type_env_["int128"]   = make_prim(TokenKind::KwInt128, 16);
    type_env_["uint8"]    = make_prim(TokenKind::KwUint8, 1);
    type_env_["uint16"]   = make_prim(TokenKind::KwUint16, 2);
    type_env_["uint32"]   = make_prim(TokenKind::KwUint32, 4);
    type_env_["uint64"]   = make_prim(TokenKind::KwUint64, 8);
    type_env_["uint128"]  = make_prim(TokenKind::KwUint128, 16);
    type_env_["float16"]  = make_prim(TokenKind::KwFloat16, 2);
    type_env_["float32"]  = make_prim(TokenKind::KwFloat32, 4);
    type_env_["float64"]  = make_prim(TokenKind::KwFloat64, 8);
    type_env_["float128"] = make_prim(TokenKind::KwFloat128, 16);
    type_env_["bool8"]    = make_prim(TokenKind::KwBool8, 1);
    type_env_["char8"]    = make_prim(TokenKind::KwChar8, 1);
    type_env_["char16"]   = make_prim(TokenKind::KwChar16, 2);
    type_env_["char32"]   = make_prim(TokenKind::KwChar32, 4);
    type_env_["string8"]  = make_prim(TokenKind::KwString8, 8);
    type_env_["string16"] = make_prim(TokenKind::KwString16, 8);
    type_env_["string32"] = make_prim(TokenKind::KwString32, 8);
    type_env_["void"]     = make_prim(TokenKind::KwVoid, 0);

    // Built-in 'any' struct layout: { int64 data; uint64 type_id; }
    StructTypeInfo any_info;
    any_info.name = "Any";
    any_info.fields = {
        StructFieldInfo{"data", type_env_["int64"], 0},
        StructFieldInfo{"type_id", type_env_["uint64"], 8}
    };
    any_info.field_map["data"] = 0;
    any_info.field_map["type_id"] = 1;
    auto* any_type = new SemaType{SemaType::Kind::Struct, 16, 8, any_info};
    type_env_["any"] = any_type;
    type_env_["Any"] = any_type;
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

    std::string orig_name = std::string(generic_ty->custom_name);
    ASTStructDecl* orig = nullptr;
    auto it = generic_structs_.find(orig_name);
    if (it != generic_structs_.end()) {
        orig = it->second;
    } else {
        for (auto& [g_name, g_st] : generic_structs_) {
            if (g_name == orig_name || g_name.ends_with("::" + orig_name)) {
                orig = g_st;
                break;
            }
        }
    }
    if (!orig) return mangled;

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

    std::string orig_name = std::string(generic_ty->custom_name);
    ASTUnionDecl* orig = nullptr;
    auto it = generic_unions_.find(orig_name);
    if (it != generic_unions_.end()) {
        orig = it->second;
    } else {
        for (auto& [g_name, g_un] : generic_unions_) {
            if (g_name == orig_name || g_name.ends_with("::" + orig_name)) {
                orig = g_un;
                break;
            }
        }
    }
    if (!orig) return mangled;

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

    ASTFunctionDecl* orig_fn = nullptr;
    auto it = generic_functions_.find(orig_name);
    if (it != generic_functions_.end()) {
        orig_fn = it->second;
    } else {
        for (auto& [g_name, g_fn] : generic_functions_) {
            if (g_name == orig_name || g_name.ends_with("::" + orig_name)) {
                orig_fn = g_fn;
                break;
            }
        }
    }
    if (!orig_fn) return orig_name;

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
    if (s == "null") return 0;
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
    if (expr->kind == ExprKind::BuiltinLine) {
        return expr->evaluated_line;
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
        if (expr->op == "==" || expr->op == "!=") {
            bool left_is_str = (expr->left && (expr->left->kind == ExprKind::BuiltinTarget || expr->left->kind == ExprKind::BuiltinArch || expr->left->kind == ExprKind::BuiltinEndian || (expr->left->kind == ExprKind::Literal && !expr->left->raw_text.empty() && (expr->left->raw_text.front() == '"' || expr->left->raw_text.front() == '`'))));
            bool right_is_str = (expr->right && (expr->right->kind == ExprKind::BuiltinTarget || expr->right->kind == ExprKind::BuiltinArch || expr->right->kind == ExprKind::BuiltinEndian || (expr->right->kind == ExprKind::Literal && !expr->right->raw_text.empty() && (expr->right->raw_text.front() == '"' || expr->right->raw_text.front() == '`'))));
            if (left_is_str || right_is_str) {
                std::string sl = eval_const_string_val(expr->left);
                std::string sr = eval_const_string_val(expr->right);
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

SemaType* TypeChecker::resolve_ast_type(ASTType* ast_ty) {
    if (!ast_ty) return nullptr;
    if (ast_ty->kind == TypeKind::Primitive) {
        for (auto& [name, ty] : type_env_) {
            if (ty->kind == SemaType::Kind::Primitive && 
                std::get<PrimitiveTypeInfo>(ty->data).kind == ast_ty->primitive_kind) {
                return ty;
            }
        }
        if (ast_ty->primitive_kind == TokenKind::KwAny) {
            return type_env_["any"];
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
    if (types_are_equal_or_compatible(expected, actual, type_env_)) {
        return true;
    }
    diag_.report_error(span, "type mismatch: cannot implicitly convert between distinct types");
    return false;
}

void TypeChecker::check_statement(ASTStmt* stmt, SemaType* return_type, ASTProgram& prog) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::VarDecl: {
            auto* declared_t = resolve_ast_type(stmt->type_annot);
            symbol_table_[std::string(stmt->name)] = SymbolInfo{ declared_t, stmt->is_const };
            if (stmt->init_expr) {
                auto* init_t = check_expression(stmt->init_expr, prog);
                if (declared_t && init_t) {
                    if (stmt->init_expr->kind != ExprKind::StructLiteral &&
                        stmt->init_expr->kind != ExprKind::ArrayLiteral &&
                        stmt->init_expr->kind != ExprKind::SliceSubrange) {
                        check_type_compatibility(declared_t, init_t, stmt->span);
                    }
                }
            }
            break;
        }
        case StmtKind::Assignment: {
            SemaType* tgt_t = nullptr;
            if (stmt->target_expr) {
                if (stmt->target_expr->kind == ExprKind::Identifier) {
                    auto it = symbol_table_.find(std::string(stmt->target_expr->raw_text));
                    if (it != symbol_table_.end() && it->second.is_const) {
                        diag_.report_error(stmt->span, "cannot mutate immutable const variable '" + std::string(stmt->target_expr->raw_text) + "'");
                    }
                }
                tgt_t = check_expression(stmt->target_expr, prog);
            }
            if (stmt->value_expr) {
                auto* val_t = check_expression(stmt->value_expr, prog);
                if (tgt_t && val_t) {
                    check_type_compatibility(tgt_t, val_t, stmt->span);
                }
            }
            break;
        }
        case StmtKind::CompoundAssignment:
        case StmtKind::Increment:
        case StmtKind::Decrement: {
            if (stmt->target_expr) {
                if (stmt->target_expr->kind == ExprKind::Identifier) {
                    auto it = symbol_table_.find(std::string(stmt->target_expr->raw_text));
                    if (it != symbol_table_.end() && it->second.is_const) {
                        diag_.report_error(stmt->span, "cannot mutate immutable const variable '" + std::string(stmt->target_expr->raw_text) + "'");
                    }
                }
                check_expression(stmt->target_expr, prog);
            }
            if (stmt->value_expr) {
                check_expression(stmt->value_expr, prog);
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
        case StmtKind::Defer: {
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
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
            symbol_table_[std::string(stmt->iter_var)] = SymbolInfo{ item_t, false };
            if (!stmt->iter_idx.empty()) {
                symbol_table_[std::string(stmt->iter_idx)] = SymbolInfo{ type_env_["int32"], false };
            }
            for (auto* s : stmt->then_block) check_statement(s, return_type, prog);
            current_loop_depth_--;
            break;
        }
        case StmtKind::ResultBranch: {
            check_expression(stmt->condition, prog);
            symbol_table_[std::string(stmt->success_var)] = SymbolInfo{ type_env_["int32"], false };
            for (auto* s : stmt->success_block) check_statement(s, return_type, prog);
            symbol_table_[std::string(stmt->failure_var)] = SymbolInfo{ type_env_["int32"], false };
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
            if (expr->raw_text == "null") {
                return new SemaType{SemaType::Kind::Pointer, 8, 8, PointerTypeInfo{type_env_["void"]}};
            }
            if (expr->raw_text == "true" || expr->raw_text == "false") {
                return type_env_["bool8"];
            }
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
        case ExprKind::BuiltinLine:
            return type_env_["int32"];
        case ExprKind::BuiltinTypeof:
        case ExprKind::BuiltinFile:
        case ExprKind::BuiltinTarget:
        case ExprKind::BuiltinArch:
        case ExprKind::BuiltinEndian:
            if (expr->left) check_expression(expr->left, prog);
            return type_env_["string8"];
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
            auto fc_it = float_const_defs_.find(std::string(expr->raw_text));
            if (fc_it != float_const_defs_.end()) {
                return type_env_["float64"];
            }
            auto it = symbol_table_.find(std::string(expr->raw_text));
            if (it != symbol_table_.end()) return it->second.type;
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
                if (base_t->is_primitive(TokenKind::KwString8)) {
                    return type_env_["char8"];
                }
                if (base_t->is_primitive(TokenKind::KwString16)) {
                    return type_env_["char16"];
                }
                if (base_t->is_primitive(TokenKind::KwString32)) {
                    return type_env_["char32"];
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
            uint64_t count = expr->is_repeat_fill ? 1 : expr->args.size();
            auto* arr_t = new SemaType{SemaType::Kind::Array, (uint32_t)(elem_t->size_bytes * count), elem_t->align_bytes, ArrayTypeInfo{elem_t, count}};
            return arr_t;
        }
        case ExprKind::Binary: {
            auto* lt = check_expression(expr->left, prog);
            auto* rt = check_expression(expr->right, prog);
            if ((lt && lt->is_floating_point()) || (rt && rt->is_floating_point())) {
                if (expr->op == "==" || expr->op == "!=") {
                    return type_env_["int32"];
                }
                if (expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
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
            if ((lt && lt->is_128bit()) || (rt && rt->is_128bit())) {
                if (expr->op == "==" || expr->op == "!=") {
                    return type_env_["int32"];
                }
                if (expr->op == "<" || expr->op == "<=" || expr->op == ">" || expr->op == ">=") {
                    return type_env_["int32"];
                }
                return type_env_["int128"];
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
                    if (fn->name == callee || fn->name.ends_with("::" + callee)) {
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

    for (auto* c : prog.constants) {
        if (c->init_expr) {
            auto* c_ty = check_expression(c->init_expr, prog);
            if (c_ty && c_ty->is_floating_point()) {
                double fval = 0.0;
                if (c->init_expr->kind == ExprKind::Literal) {
                    if (c->init_expr->raw_text != "true" && c->init_expr->raw_text != "false") {
                        std::string clean;
                        for (char ch : c->init_expr->raw_text) if (ch != '_') clean.push_back(ch);
                        fval = std::stod(clean);
                    }
                }
                float_const_defs_[std::string(c->name)] = fval;
            } else {
                const_defs_[std::string(c->name)] = eval_const_expr(c->init_expr);
            }
        }
    }

    for (auto* e : prog.enums) {
        for (auto& v : e->variants) {
            enum_defs_[std::string(e->name)][std::string(v.name)] = v.value.value_or(0);
        }
    }

    for (auto* st : prog.structs) {
        if (!st->generic_params.empty()) {
            generic_structs_[std::string(st->name)] = st;
        } else {
            compute_struct_layout(st);
        }
    }

    for (auto* un : prog.unions) {
        if (!un->generic_params.empty()) {
            generic_unions_[std::string(un->name)] = un;
        } else {
            compute_union_layout(un);
        }
    }

    for (auto* fn : prog.functions) {
        if (!fn->generic_params.empty()) {
            generic_functions_[std::string(fn->name)] = fn;
        }
    }

    size_t i = 0;
    while (i < prog.functions.size()) {
        auto* fn = prog.functions[i];
        if (fn->generic_params.empty() && !fn->is_extern_c) {
            symbol_table_.clear();
            current_loop_depth_ = 0;
            for (auto& p : fn->params) {
                if (p.is_variadic_slice) {
                    auto* elem = resolve_ast_type(p.type);
                    auto* slice_t = new SemaType{SemaType::Kind::Slice, 16, 8, SliceTypeInfo{elem}};
                    symbol_table_[std::string(p.name)] = SymbolInfo{ slice_t, false };
                } else {
                    symbol_table_[std::string(p.name)] = SymbolInfo{ resolve_ast_type(p.type), false };
                }
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