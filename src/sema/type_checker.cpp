#include "sema/type_checker.hpp"

namespace femto {

void TypeChecker::init_primitives() {
    auto make_prim = [&](TokenKind k, uint32_t sz) {
        auto* t = new SemaType{SemaType::Kind::Primitive, sz, sz, PrimitiveTypeInfo{k}};
        return t;
    };

    type_env_["int8"]    = make_prim(TokenKind::KwInt8, 1);
    type_env_["int16"]   = make_prim(TokenKind::KwInt16, 2);
    type_env_["int32"]   = make_prim(TokenKind::KwInt32, 4);
    type_env_["int64"]   = make_prim(TokenKind::KwInt64, 8);
    type_env_["uint8"]   = make_prim(TokenKind::KwUint8, 1);
    type_env_["uint16"]  = make_prim(TokenKind::KwUint16, 2);
    type_env_["uint32"]  = make_prim(TokenKind::KwUint32, 4);
    type_env_["uint64"]  = make_prim(TokenKind::KwUint64, 8);
    type_env_["float32"] = make_prim(TokenKind::KwFloat32, 4);
    type_env_["float64"] = make_prim(TokenKind::KwFloat64, 8);
    type_env_["bool8"]   = make_prim(TokenKind::KwBool8, 1);
    type_env_["char8"]   = make_prim(TokenKind::KwChar8, 1);
}

void TypeChecker::compute_struct_layout(ASTStructDecl* decl) {
    StructTypeInfo info;
    info.name = std::string(decl->name);

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
    type_env_[std::string(decl->name)] = st;
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
        uint32_t sz = 4 + (payload ? payload->size_bytes : 0);
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

void TypeChecker::check_statement(ASTStmt* stmt, SemaType* return_type) {
    if (!stmt) return;
    switch (stmt->kind) {
        case StmtKind::VarDecl: {
            auto* declared_t = resolve_ast_type(stmt->type_annot);
            symbol_table_[std::string(stmt->name)] = declared_t;
            if (stmt->init_expr) {
                check_expression(stmt->init_expr);
            }
            break;
        }
        case StmtKind::Assignment:
        case StmtKind::CompoundAssignment: {
            if (stmt->target_expr) {
                check_expression(stmt->target_expr);
            }
            if (stmt->value_expr) {
                check_expression(stmt->value_expr);
            }
            break;
        }
        case StmtKind::Increment:
        case StmtKind::Decrement: {
            if (stmt->target_expr) {
                check_expression(stmt->target_expr);
            }
            break;
        }
        case StmtKind::If: {
            check_expression(stmt->condition);
            for (auto* s : stmt->then_block) check_statement(s, return_type);
            for (auto* s : stmt->else_block) check_statement(s, return_type);
            break;
        }
        case StmtKind::While:
        case StmtKind::DoWhile: {
            current_loop_depth_++;
            check_expression(stmt->condition);
            for (auto* s : stmt->then_block) check_statement(s, return_type);
            current_loop_depth_--;
            break;
        }
        case StmtKind::Switch: {
            current_loop_depth_++;
            check_expression(stmt->condition);
            for (auto& sc : stmt->switch_cases) {
                if (sc.match_val) check_expression(sc.match_val);
                for (auto* s : sc.body) check_statement(s, return_type);
            }
            current_loop_depth_--;
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
                check_expression(stmt->value_expr);
            }
            break;
        }
        case StmtKind::ExprStmt: {
            if (stmt->value_expr) {
                check_expression(stmt->value_expr);
            }
            break;
        }
        default:
            break;
    }
}

SemaType* TypeChecker::check_expression(ASTExpr* expr) {
    if (!expr) return nullptr;

    switch (expr->kind) {
        case ExprKind::Literal:
            return type_env_["int32"];
        case ExprKind::Identifier: {
            auto it = symbol_table_.find(std::string(expr->raw_text));
            if (it != symbol_table_.end()) return it->second;
            return type_env_["int32"];
        }
        case ExprKind::Unary: {
            auto* sub = check_expression(expr->left);
            if (expr->op == "&" && sub) {
                auto* pt = new SemaType{SemaType::Kind::Pointer, 8, 8, PointerTypeInfo{sub}};
                return pt;
            }
            if (expr->op == "*" && sub && sub->kind == SemaType::Kind::Pointer) {
                return std::get<PointerTypeInfo>(sub->data).pointee;
            }
            return sub;
        }
        case ExprKind::MemberAccess: {
            auto* base_t = check_expression(expr->left);
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
                if (base_t && base_t->kind == SemaType::Kind::Slice && expr->raw_text == "length") {
                    return type_env_["int32"];
                }
            }
            return type_env_["int32"];
        }
        case ExprKind::Index: {
            auto* base_t = check_expression(expr->left);
            check_expression(expr->right);
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
            auto* base_t = check_expression(expr->left);
            for (auto* a : expr->args) check_expression(a);
            if (base_t && base_t->kind == SemaType::Kind::Array) {
                auto* elem = std::get<ArrayTypeInfo>(base_t->data).element;
                auto* slice_t = new SemaType{SemaType::Kind::Slice, 16, 8, SliceTypeInfo{elem}};
                return slice_t;
            }
            return base_t;
        }
        case ExprKind::StructLiteral: {
            for (auto& sf : expr->struct_fields) {
                check_expression(sf.second);
            }
            return type_env_["int32"];
        }
        case ExprKind::ArrayLiteral: {
            SemaType* elem_t = nullptr;
            for (auto* a : expr->args) {
                elem_t = check_expression(a);
            }
            if (!elem_t) elem_t = type_env_["int32"];
            auto* arr_t = new SemaType{SemaType::Kind::Array, (uint32_t)(elem_t->size_bytes * expr->args.size()), elem_t->align_bytes, ArrayTypeInfo{elem_t, expr->args.size()}};
            return arr_t;
        }
        case ExprKind::Binary: {
            check_expression(expr->left);
            check_expression(expr->right);
            return type_env_["int32"];
        }
        case ExprKind::Call: {
            for (auto* a : expr->args) check_expression(a);
            return type_env_["int32"];
        }
        default:
            return type_env_["int32"];
    }
}

bool TypeChecker::check_program(ASTProgram& prog) {
    init_primitives();

    for (auto* st : prog.structs) {
        compute_struct_layout(st);
    }

    for (auto* fn : prog.functions) {
        symbol_table_.clear();
        current_loop_depth_ = 0;
        for (auto& p : fn->params) {
            symbol_table_[std::string(p.name)] = resolve_ast_type(p.type);
        }
        auto* ret_type = resolve_ast_type(fn->return_type);
        for (auto* stmt : fn->body) {
            check_statement(stmt, ret_type);
        }
    }
    return !diag_.has_errors();
}

} // namespace femto