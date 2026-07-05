#include "type_checker.h"

#include <sstream>

namespace femto::sema {

TypeChecker::TypeChecker(DiagnosticEngine& diag) : diag_(diag) {
    // Register builtin types in the global scope
    symbols_.insert({"int8", SymbolKind::Struct, make_int(8), {}, false, true});
    symbols_.insert({"int16", SymbolKind::Struct, make_int(16), {}, false, true});
    symbols_.insert({"int32", SymbolKind::Struct, make_int(32), {}, false, true});
    symbols_.insert({"int64", SymbolKind::Struct, make_int(64), {}, false, true});
    symbols_.insert({"int128", SymbolKind::Struct, make_int(128), {}, false, true});
    symbols_.insert({"int256", SymbolKind::Struct, make_int(256), {}, false, true});
    symbols_.insert({"int512", SymbolKind::Struct, make_int(512), {}, false, true});
    symbols_.insert({"uint8", SymbolKind::Struct, make_uint(8), {}, false, true});
    symbols_.insert({"uint16", SymbolKind::Struct, make_uint(16), {}, false, true});
    symbols_.insert({"uint32", SymbolKind::Struct, make_uint(32), {}, false, true});
    symbols_.insert({"uint64", SymbolKind::Struct, make_uint(64), {}, false, true});
    symbols_.insert({"uint128", SymbolKind::Struct, make_uint(128), {}, false, true});
    symbols_.insert({"uint256", SymbolKind::Struct, make_uint(256), {}, false, true});
    symbols_.insert({"uint512", SymbolKind::Struct, make_uint(512), {}, false, true});
    symbols_.insert({"float16", SymbolKind::Struct, make_float(16), {}, false, true});
    symbols_.insert({"float32", SymbolKind::Struct, make_float(32), {}, false, true});
    symbols_.insert({"float64", SymbolKind::Struct, make_float(64), {}, false, true});
    symbols_.insert({"float128", SymbolKind::Struct, make_float(128), {}, false, true});
    symbols_.insert({"bool8", SymbolKind::Struct, make_bool(8), {}, false, true});
    symbols_.insert({"bool16", SymbolKind::Struct, make_bool(16), {}, false, true});
    symbols_.insert({"bool32", SymbolKind::Struct, make_bool(32), {}, false, true});
    symbols_.insert({"bool64", SymbolKind::Struct, make_bool(64), {}, false, true});
    symbols_.insert({"bool128", SymbolKind::Struct, make_bool(128), {}, false, true});
    symbols_.insert({"bool256", SymbolKind::Struct, make_bool(256), {}, false, true});
    symbols_.insert({"bool512", SymbolKind::Struct, make_bool(512), {}, false, true});
    symbols_.insert({"char8", SymbolKind::Struct, make_char(8), {}, false, true});
    symbols_.insert({"char16", SymbolKind::Struct, make_char(16), {}, false, true});
    symbols_.insert({"char32", SymbolKind::Struct, make_char(32), {}, false, true});
    symbols_.insert({"string8", SymbolKind::Struct, make_string(8), {}, false, true});
    symbols_.insert({"string16", SymbolKind::Struct, make_string(16), {}, false, true});
    symbols_.insert({"string32", SymbolKind::Struct, make_string(32), {}, false, true});
    symbols_.insert({"void", SymbolKind::Struct, make_void(), {}, false, true});
    symbols_.insert({"true", SymbolKind::Constant, make_bool(8), {}, false, true});
    symbols_.insert({"false", SymbolKind::Constant, make_bool(8), {}, false, true});
    symbols_.insert({"null", SymbolKind::Constant, make_pointer(make_void()), {}, false, true});
}

bool TypeChecker::check(ast::Module& mod) {
    // Pass 1: Register all function/struct/enum signatures (forward declarations)
    for (auto& decl : mod.decls) {
        if (!decl) continue;
        if (auto* func = std::get_if<ast::FunctionDecl>(&decl->data)) {
            std::vector<TypePtr> param_types;
            for (auto& p : func->params) {
                param_types.push_back(resolve_type(*p.type));
            }
            TypePtr ret_type = func->return_type ? resolve_type(*func->return_type) : make_void();
            auto sig = make_function(std::vector<TypePtr>(param_types), ret_type, func->is_error_return);
            symbols_.insert({func->name, SymbolKind::Function, sig, func->body ? func->body->span : SourceSpan{}});
        } else if (auto* st = std::get_if<ast::StructDecl>(&decl->data)) {
            check_struct_decl(*st);
        } else if (auto* en = std::get_if<ast::EnumDecl>(&decl->data)) {
            check_enum_decl(*en);
        } else if (auto* un = std::get_if<ast::UnionDecl>(&decl->data)) {
            check_union_decl(*un);
        } else if (auto* ns = std::get_if<ast::NamespaceDecl>(&decl->data)) {
            check_namespace_decl(*ns);
        }
    }
    // Pass 2: Check function bodies and other declarations
    for (auto& decl : mod.decls) {
        if (decl) {
            if (auto* func = std::get_if<ast::FunctionDecl>(&decl->data)) {
                // Only check the body, signature already registered
                if (func->body) {
                    symbols_.push_scope();
                    for (auto& p : func->params) {
                        auto pt = resolve_type(*p.type);
                        symbols_.insert({p.name, SymbolKind::Variable, pt, p.name_span, false, true});
                    }
                    auto* prev_func = current_func_;
                    auto prev_ret = current_return_type_;
                    std::vector<TypePtr> param_types;
                    for (auto& p : func->params) param_types.push_back(resolve_type(*p.type));
                    TypePtr ret_type = func->return_type ? resolve_type(*func->return_type) : make_void();
                    FunctionSignature cur_sig{func->name, std::move(param_types), ret_type, func->is_error_return};
                    current_func_ = &cur_sig;
                    current_return_type_ = ret_type;
                    check_block(std::get<ast::Block>(func->body->data));
                    current_func_ = prev_func;
                    current_return_type_ = prev_ret;
                    symbols_.pop_scope();
                }
            } else {
                check_decl(*decl);
            }
        }
    }
    return !diag_.has_errors();
}

TypePtr TypeChecker::resolve_builtin_type(TokenType tt) {
    switch (tt) {
        case TokenType::TY_int8: return make_int(8);
        case TokenType::TY_int16: return make_int(16);
        case TokenType::TY_int32: return make_int(32);
        case TokenType::TY_int64: return make_int(64);
        case TokenType::TY_int128: return make_int(128);
        case TokenType::TY_int256: return make_int(256);
        case TokenType::TY_int512: return make_int(512);
        case TokenType::TY_uint8: return make_uint(8);
        case TokenType::TY_uint16: return make_uint(16);
        case TokenType::TY_uint32: return make_uint(32);
        case TokenType::TY_uint64: return make_uint(64);
        case TokenType::TY_uint128: return make_uint(128);
        case TokenType::TY_uint256: return make_uint(256);
        case TokenType::TY_uint512: return make_uint(512);
        case TokenType::TY_float16: return make_float(16);
        case TokenType::TY_float32: return make_float(32);
        case TokenType::TY_float64: return make_float(64);
        case TokenType::TY_float128: return make_float(128);
        case TokenType::TY_bool8: return make_bool(8);
        case TokenType::TY_bool16: return make_bool(16);
        case TokenType::TY_bool32: return make_bool(32);
        case TokenType::TY_bool64: return make_bool(64);
        case TokenType::TY_bool128: return make_bool(128);
        case TokenType::TY_bool256: return make_bool(256);
        case TokenType::TY_bool512: return make_bool(512);
        case TokenType::TY_char8: return make_char(8);
        case TokenType::TY_char16: return make_char(16);
        case TokenType::TY_char32: return make_char(32);
        case TokenType::TY_string8: return make_string(8);
        case TokenType::TY_string16: return make_string(16);
        case TokenType::TY_string32: return make_string(32);
        case TokenType::KW_void: return make_void();
        default: return make_error();
    }
}

TypePtr TypeChecker::resolve_type(const ast::TypeNode& node) {
    return std::visit([this](const auto& t) -> TypePtr {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, ast::PrimitiveType>) {
            return resolve_builtin_type(t.token_type);
        } else if constexpr (std::is_same_v<T, ast::PointerType>) {
            return make_pointer(resolve_type(*t.inner));
        } else if constexpr (std::is_same_v<T, ast::SliceType>) {
            return make_slice(resolve_type(*t.inner));
        } else if constexpr (std::is_same_v<T, ast::ArrayType>) {
            return make_array(resolve_type(*t.inner), 0);
        } else if constexpr (std::is_same_v<T, ast::FunctionType>) {
            std::vector<TypePtr> params;
            for (auto& p : t.param_types) params.push_back(resolve_type(*p));
            return make_function(std::move(params), resolve_type(*t.return_type), t.is_error_return);
        } else if constexpr (std::is_same_v<T, ast::NamedType>) {
            auto* sym = symbols_.lookup(t.name);
            if (sym) return sym->type;
            diag_.error(t.span.start, "unknown type '" + t.name + "'");
            return make_error();
        } else if constexpr (std::is_same_v<T, ast::GenericType>) {
            return make_generic(t.name);
        } else {
            return make_error();
        }
    }, node.data);
}

void TypeChecker::check_decl(ast::Decl& decl) {
    std::visit([this](auto& d) {
        using T = std::decay_t<decltype(d)>;
        if constexpr (std::is_same_v<T, ast::VariableDecl>) check_variable_decl(d);
        else if constexpr (std::is_same_v<T, ast::ConstantDecl>) check_constant_decl(d);
        else if constexpr (std::is_same_v<T, ast::FunctionDecl>) check_function_decl(d);
        else if constexpr (std::is_same_v<T, ast::StructDecl>) check_struct_decl(d);
        else if constexpr (std::is_same_v<T, ast::EnumDecl>) check_enum_decl(d);
        else if constexpr (std::is_same_v<T, ast::UnionDecl>) check_union_decl(d);
        else if constexpr (std::is_same_v<T, ast::NamespaceDecl>) check_namespace_decl(d);
        else if constexpr (std::is_same_v<T, ast::ExternBlock>) check_extern_block(d);
        else if constexpr (std::is_same_v<T, ast::ImportDecl>) check_import_decl(d);
    }, decl.data);
}

void TypeChecker::check_variable_decl(ast::VariableDecl& var) {
    TypePtr var_type = resolve_type(*var.type);
    TypePtr init_type;
    if (var.init) {
        init_type = check_expr(*var.init);
        if (init_type && !is_error(*init_type) && !is_error(*var_type)) {
            if (!is_assignable(*init_type, *var_type)) {
                diag_.error(var.type->span.start,
                    "type mismatch: cannot assign '" + type_to_string(*init_type) +
                    "' to '" + type_to_string(*var_type) + "'");
            }
        }
    }
    symbols_.insert({var.name, SymbolKind::Variable, var_type, var.type->span, false, var.is_const});
}

void TypeChecker::check_constant_decl(ast::ConstantDecl& con) {
    TypePtr init_type = check_expr(*con.init);
    TypePtr const_type = con.type ? resolve_type(*con.type) : init_type;
    symbols_.insert({con.name, SymbolKind::Constant, const_type, con.init->span, false, true});
}

void TypeChecker::check_function_decl(ast::FunctionDecl& func) {
    std::vector<TypePtr> param_types;
    for (auto& p : func.params) {
        param_types.push_back(resolve_type(*p.type));
    }
    TypePtr ret_type = func.return_type ? resolve_type(*func.return_type) : make_void();

    auto sig = make_function(std::vector<TypePtr>(param_types), ret_type, func.is_error_return);
    symbols_.insert({func.name, SymbolKind::Function, sig, func.body ? func.body->span : SourceSpan{}});

    if (func.body) {
        symbols_.push_scope();
        for (auto& p : func.params) {
            auto pt = resolve_type(*p.type);
            symbols_.insert({p.name, SymbolKind::Variable, pt, p.name_span, false, true});
        }
        auto* prev_func = current_func_;
        auto prev_ret = current_return_type_;
        FunctionSignature cur_sig{func.name, std::move(param_types), ret_type, func.is_error_return};
        current_func_ = &cur_sig;
        current_return_type_ = ret_type;
        check_block(std::get<ast::Block>(func.body->data));
        current_func_ = prev_func;
        current_return_type_ = prev_ret;
        symbols_.pop_scope();
    }
}

void TypeChecker::check_struct_decl(ast::StructDecl& str) {
    symbols_.insert({str.name, SymbolKind::Struct, make_named(str.name), str.fields.empty() ? SourceSpan{} : SourceSpan{}});
}

void TypeChecker::check_enum_decl(ast::EnumDecl& en) {
    symbols_.insert({en.name, SymbolKind::Enum, make_named(en.name), {}});
}

void TypeChecker::check_union_decl(ast::UnionDecl& un) {
    symbols_.insert({un.name, SymbolKind::Union, make_named(un.name), {}});
}

void TypeChecker::check_namespace_decl(ast::NamespaceDecl& ns) {
    symbols_.push_scope();
    for (auto& d : ns.decls) {
        if (d) check_decl(*d);
    }
    symbols_.pop_scope();
}

void TypeChecker::check_extern_block(ast::ExternBlock& block) {
    for (auto& d : block.decls) {
        if (d) check_decl(*d);
    }
}

void TypeChecker::check_import_decl(ast::ImportDecl& imp) {
    // Module loading is handled separately; for now, just register the import
}

TypePtr TypeChecker::check_stmt(ast::Stmt& stmt) {
    return std::visit([this](auto& s) -> TypePtr {
        using T = std::decay_t<decltype(s)>;
        if constexpr (std::is_same_v<T, ast::ExprStmt>) {
            return check_expr(*s.expr);
        } else if constexpr (std::is_same_v<T, ast::AssignStmt>) {
            return check_assign_expr(s);
        } else if constexpr (std::is_same_v<T, ast::CompoundAssignStmt>) {
            auto lt = check_expr(*s.target);
            auto rt = check_expr(*s.value);
            return lt;
        } else if constexpr (std::is_same_v<T, ast::ReturnStmt>) {
            return check_return_stmt(s);
        } else if constexpr (std::is_same_v<T, ast::IfStmt>) {
            return check_if_stmt(s);
        } else if constexpr (std::is_same_v<T, ast::WhileStmt>) {
            return check_while_stmt(s);
        } else if constexpr (std::is_same_v<T, ast::DoWhileStmt>) {
            return check_do_while_stmt(s);
        } else if constexpr (std::is_same_v<T, ast::SwitchStmt>) {
            return check_switch_stmt(s);
        } else if constexpr (std::is_same_v<T, ast::Block>) {
            symbols_.push_scope();
            auto result = check_block(s);
            symbols_.pop_scope();
            return result;
        } else if constexpr (std::is_same_v<T, ast::BreakStmt> || std::is_same_v<T, ast::ContinueStmt>) {
            return make_void();
        } else if constexpr (std::is_same_v<T, ast::ForeachStmt>) {
            symbols_.push_scope();
            auto elem_type = resolve_type(*s.element_type);
            symbols_.insert({s.element_var, SymbolKind::Variable, elem_type, {}, false, true});
            auto result = check_block(std::get<ast::Block>(s.body->data));
            symbols_.pop_scope();
            return result;
        } else if constexpr (std::is_same_v<T, ast::MatchStmt>) {
            check_expr(*s.subject);
            for (auto& arm : s.arms) {
                check_expr(*arm.condition);
                check_expr(*arm.value);
            }
            return make_void();
        } else if constexpr (std::is_same_v<T, std::unique_ptr<ast::Decl>>) {
            if (s) check_decl(*s);
            return make_void();
        } else {
            return make_void();
        }
    }, stmt.data);
}

TypePtr TypeChecker::check_block(ast::Block& block) {
    TypePtr last = make_void();
    for (auto& stmt : block.stmts) {
        last = check_stmt(*stmt);
    }
    return last;
}

TypePtr TypeChecker::check_if_stmt(ast::IfStmt& stmt) {
    auto cond_type = check_expr(*stmt.condition);
    check_stmt(*stmt.then_block);
    if (stmt.else_block) check_stmt(**stmt.else_block);
    return make_void();
}

TypePtr TypeChecker::check_while_stmt(ast::WhileStmt& stmt) {
    check_expr(*stmt.condition);
    check_stmt(*stmt.body);
    return make_void();
}

TypePtr TypeChecker::check_do_while_stmt(ast::DoWhileStmt& stmt) {
    check_stmt(*stmt.body);
    check_expr(*stmt.condition);
    return make_void();
}

TypePtr TypeChecker::check_switch_stmt(ast::SwitchStmt& stmt) {
    check_expr(*stmt.subject);
    for (auto& c : stmt.cases) {
        check_expr(*c.condition);
        check_stmt(*c.body);
    }
    if (stmt.default_case) check_stmt(**stmt.default_case);
    return make_void();
}

TypePtr TypeChecker::check_return_stmt(ast::ReturnStmt& stmt) {
    if (stmt.value) {
        auto val_type = check_expr(**stmt.value);
        if (current_return_type_ && !is_error(*current_return_type_) && !is_error(*val_type)) {
            if (!is_assignable(*val_type, *current_return_type_)) {
                diag_.error(stmt.value->get()->span.start,
                    "return type mismatch: '" + type_to_string(*val_type) +
                    "' is not assignable to '" + type_to_string(*current_return_type_) + "'");
            }
        }
    }
    return make_void();
}

TypePtr TypeChecker::check_expr(ast::Expr& expr) {
    return std::visit([this](auto& e) -> TypePtr {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, ast::IntegerLit>) {
            return make_int(64); // default integer size
        } else if constexpr (std::is_same_v<T, ast::FloatLit>) {
            return make_float(64);
        } else if constexpr (std::is_same_v<T, ast::CharLit>) {
            return make_char(8);
        } else if constexpr (std::is_same_v<T, ast::StringLit>) {
            return make_string(8);
        } else if constexpr (std::is_same_v<T, ast::BoolLit>) {
            return make_bool(8);
        } else if constexpr (std::is_same_v<T, ast::NullLit>) {
            return make_pointer(make_void());
        } else if constexpr (std::is_same_v<T, ast::Identifier>) {
            auto* sym = symbols_.lookup(e.name);
            if (!sym) {
                diag_.error(e.span.start, "undefined identifier '" + e.name + "'");
                return make_error();
            }
            return sym->type;
        } else if constexpr (std::is_same_v<T, ast::BinaryExpr>) {
            return check_binary_expr(e);
        } else if constexpr (std::is_same_v<T, ast::UnaryExpr>) {
            return check_unary_expr(e);
        } else if constexpr (std::is_same_v<T, ast::CallExpr>) {
            return check_call_expr(e);
        } else if constexpr (std::is_same_v<T, ast::CastExpr>) {
            return resolve_type(*e.target_type);
        } else if constexpr (std::is_same_v<T, ast::MemberExpr>) {
            auto obj_type = check_expr(*e.object);
            if (is_error(*obj_type)) return make_error();
            // For now, return a generic named type for member access
            return make_named(e.member);
        } else if constexpr (std::is_same_v<T, ast::IndexExpr>) {
            check_expr(*e.index);
            auto obj_type = check_expr(*e.object);
            if (obj_type->kind == TypeKind::Slice) return std::get<SliceType>(obj_type->data).inner;
            if (obj_type->kind == TypeKind::Array) return std::get<ArrayType>(obj_type->data).inner;
            return make_error();
        } else if constexpr (std::is_same_v<T, ast::SuccessExpr>) {
            return check_expr(*e.value);
        } else if constexpr (std::is_same_v<T, ast::FailureExpr>) {
            return make_error(); // failure type depends on context
        } else if constexpr (std::is_same_v<T, ast::ResultBranchExpr>) {
            auto inner_type = check_expr(*e.expr);
            // Bare propagation: expr?? (null branches) — unwrap error type
            if (!e.on_success && !e.on_failure) {
                if (inner_type && inner_type->kind == TypeKind::Function) {
                    auto& ft = std::get<FunctionType>(inner_type->data);
                    if (ft.is_error_return) return ft.return_type;
                }
                return inner_type ? inner_type : make_void();
            }
            // Branch form: check arms
            if (e.on_success) check_expr(*e.on_success);
            if (e.on_failure) check_expr(*e.on_failure);
            return make_void();
        } else if constexpr (std::is_same_v<T, ast::BuiltinExpr>) {
            return make_void();
        } else if constexpr (std::is_same_v<T, ast::ArrayLiteral>) {
            if (!e.elements.empty()) return check_expr(*e.elements[0]);
            return make_error();
        } else {
            return make_void();
        }
    }, expr.data);
}

TypePtr TypeChecker::check_binary_expr(ast::BinaryExpr& expr) {
    auto left = check_expr(*expr.left);
    auto right = check_expr(*expr.right);

    switch (expr.op) {
        case ast::BinaryExpr::Add:
        case ast::BinaryExpr::Sub:
        case ast::BinaryExpr::Mul:
        case ast::BinaryExpr::Div:
        case ast::BinaryExpr::Mod:
            if (is_numeric(*left) && is_numeric(*right)) return left;
            diag_.error(expr.left->span.start, "arithmetic operators require numeric types");
            return make_error();

        case ast::BinaryExpr::BitAnd:
        case ast::BinaryExpr::BitOr:
        case ast::BinaryExpr::BitXor:
            if (is_integer(*left) && is_integer(*right)) return left;
            diag_.error(expr.left->span.start, "bitwise operators require integer types");
            return make_error();

        case ast::BinaryExpr::LShift:
        case ast::BinaryExpr::RShift:
            if (is_integer(*left) && is_integer(*right)) return left;
            diag_.error(expr.left->span.start, "shift operators require integer types");
            return make_error();

        case ast::BinaryExpr::Eq:
        case ast::BinaryExpr::Neq:
        case ast::BinaryExpr::Lt:
        case ast::BinaryExpr::Gt:
        case ast::BinaryExpr::Le:
        case ast::BinaryExpr::Ge:
            return make_bool(8);

        case ast::BinaryExpr::LogicAnd:
        case ast::BinaryExpr::LogicOr:
            return make_bool(8);

        default:
            return make_error();
    }
}

TypePtr TypeChecker::check_unary_expr(ast::UnaryExpr& expr) {
    auto operand_type = check_expr(*expr.operand);

    switch (expr.op) {
        case ast::UnaryExpr::Neg:
        case ast::UnaryExpr::Not:
        case ast::UnaryExpr::BitNot:
            return operand_type;
        case ast::UnaryExpr::Deref:
            if (operand_type->kind == TypeKind::Pointer)
                return std::get<PointerType>(operand_type->data).inner;
            diag_.error(expr.operand->span.start, "cannot dereference non-pointer type");
            return make_error();
        case ast::UnaryExpr::Addr:
            return make_pointer(operand_type);
        case ast::UnaryExpr::PreInc:
        case ast::UnaryExpr::PreDec:
            return operand_type;
    }
    return make_error();
}

TypePtr TypeChecker::check_call_expr(ast::CallExpr& expr) {
    if (auto* id = std::get_if<ast::Identifier>(&expr.callee->data)) {
        std::vector<TypePtr> arg_types;
        for (auto& arg : expr.args) {
            arg_types.push_back(check_expr(*arg));
        }

        auto* sym = symbols_.lookup(id->name);
        if (!sym) {
            diag_.error(expr.callee->span.start, "undefined function '" + id->name + "'");
            return make_error();
        }

        if (sym->kind != SymbolKind::Function) {
            diag_.error(expr.callee->span.start, "'" + id->name + "' is not a function");
            return make_error();
        }

        if (sym->type->kind == TypeKind::Function) {
            auto& ft = std::get<FunctionType>(sym->type->data);
            if (arg_types.size() != ft.param_types.size()) {
                diag_.error(expr.callee->span.start,
                    "expected " + std::to_string(ft.param_types.size()) +
                    " arguments, got " + std::to_string(arg_types.size()));
                return make_error();
            }
            return ft.return_type;
        }
    }

    // For MemberExpr callee (e.g. std.io.print), resolve to the function
    if (auto* member = std::get_if<ast::MemberExpr>(&expr.callee->data)) {
        // Build qualified name by walking the member chain
        std::string qualified_name;
        auto* cur = member;
        while (cur) {
            if (!qualified_name.empty()) qualified_name = "." + qualified_name;
            qualified_name = cur->member + qualified_name;
            if (auto* inner_id = std::get_if<ast::Identifier>(&cur->object->data)) {
                qualified_name = inner_id->name + "." + qualified_name;
                break;
            }
            if (auto* inner_mem = std::get_if<ast::MemberExpr>(&cur->object->data)) {
                cur = inner_mem;
            } else break;
        }

        auto* sym = symbols_.lookup(qualified_name);
        if (sym && sym->kind == SymbolKind::Function && sym->type->kind == TypeKind::Function) {
            auto& ft = std::get<FunctionType>(sym->type->data);
            std::vector<TypePtr> arg_types;
            for (auto& arg : expr.args) arg_types.push_back(check_expr(*arg));
            if (arg_types.size() != ft.param_types.size()) {
                diag_.error(expr.callee->span.start,
                    "expected " + std::to_string(ft.param_types.size()) +
                    " arguments, got " + std::to_string(arg_types.size()));
                return make_error();
            }
            return ft.return_type;
        }
    }

    // For GenericCallExpr callee
    if (auto* gc = std::get_if<ast::GenericCallExpr>(&expr.callee->data)) {
        return check_call_expr(expr); // recurse with inner callee
    }

    // For other callee types, check each argument
    for (auto& arg : expr.args) check_expr(*arg);
    return make_void();
}

TypePtr TypeChecker::check_assign_expr(ast::AssignStmt& stmt) {
    auto target_type = check_expr(*stmt.target);
    auto value_type = check_expr(*stmt.value);
    if (!is_error(*target_type) && !is_error(*value_type) && !is_assignable(*value_type, *target_type)) {
        diag_.error(stmt.value->span.start,
            "type mismatch in assignment: '" + type_to_string(*value_type) +
            "' is not assignable to '" + type_to_string(*target_type) + "'");
    }
    return target_type;
}

} // namespace femto::sema
