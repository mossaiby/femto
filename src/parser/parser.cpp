#include "parser.h"

#include <charconv>

namespace femto {

using ast::ExprPtr;
using ast::StmtPtr;
using ast::DeclPtr;
using ast::TypePtr;
using ast::Block;

Parser::Parser(const std::vector<Token>& tokens, DiagnosticEngine& diag)
    : tokens_(tokens), diag_(diag) {}

ast::Module Parser::parse() {
    ast::Module mod;
    while (!is_at_end()) {
        auto decl = parse_decl();
        if (decl) mod.decls.push_back(std::move(decl));
    }
    return mod;
}

const Token& Parser::peek() const { return tokens_[pos_]; }
const Token& Parser::peek_next() const {
    return (pos_ + 1 < tokens_.size()) ? tokens_[pos_ + 1] : tokens_.back();
}
const Token& Parser::advance() {
    if (!is_at_end()) ++pos_;
    return tokens_[pos_ - 1];
}
bool Parser::match(TokenType type) { if (check(type)) { advance(); return true; } return false; }
bool Parser::check(TokenType type) const { return peek().type == type; }
bool Parser::is_at_end() const { return peek().type == TokenType::Eof; }

void Parser::error(const std::string& msg) {
    diag_.error(peek().span.start, msg);
}

void Parser::expect(TokenType type, const std::string& expected) {
    if (!match(type)) {
        error("expected " + expected + ", got '" + peek().lexeme + "'");
        while (!is_at_end() && !check(TokenType::Semicolon) && !check(TokenType::RBrace)) advance();
        match(TokenType::Semicolon);
    }
}

void Parser::synchronize() {
    while (!is_at_end()) {
        if (check(TokenType::Semicolon)) { advance(); return; }
        if (check(TokenType::RBrace)) return;
        advance();
    }
}

// ---- Helpers ----

std::string Parser::parse_name() {
    if (check(TokenType::Identifier)) return advance().lexeme;
    error("expected identifier, got '" + peek().lexeme + "'");
    return "error";
}

bool Parser::is_builtin_type_token() const { return is_builtin_type(peek().type); }
bool Parser::is_type_start() const { return is_builtin_type_token() || check(TokenType::KW_void); }
bool Parser::check_type() const {
    return is_type_start() ||
           (check(TokenType::Identifier) && peek_next().type == TokenType::Identifier);
}

// ---- Top-level declarations ----

DeclPtr Parser::parse_decl() {
    bool exported = false;
    if (check(TokenType::HashExport)) { advance(); exported = true; }
    if (check(TokenType::HashIf)) { auto d = parse_compile_time_if(); d->is_exported = exported; return d; }
    if (check(TokenType::KW_import)) return parse_import_decl();
    if (check(TokenType::KW_extern)) return parse_extern_block();
    if (check(TokenType::KW_namespace)) return parse_namespace_decl();

    // struct/enum/union
    if (check(TokenType::KW_struct)) { auto d = parse_struct_decl(); d->is_exported = exported; return d; }
    if (check(TokenType::KW_enum)) { auto d = parse_enum_decl(); d->is_exported = exported; return d; }
    if (check(TokenType::KW_union)) { auto d = parse_union_decl(); d->is_exported = exported; return d; }

    // const variable: const type name = expr;
    if (check(TokenType::KW_const)) {
        auto d = parse_variable_decl();
        if (d) { d->is_exported = exported; }
        return d;
    }

    // name :: ... (constant or function)
    if (check(TokenType::Identifier)) {
        std::size_t saved = pos_;
        advance(); // name

        if (check(TokenType::DoubleColon)) {
            std::size_t after_dc = pos_ + 1;
            bool is_function = false;
            bool is_struct = false;
            bool is_enum = false;
            bool is_union = false;

            if (after_dc < tokens_.size()) {
                auto t = tokens_[after_dc].type;
                is_struct = (t == TokenType::KW_struct);
                is_enum = (t == TokenType::KW_enum);
                is_union = (t == TokenType::KW_union);
            }

            if (!is_struct && !is_enum && !is_union) {
                if (after_dc < tokens_.size() && tokens_[after_dc].type == TokenType::Lt) {
                    is_function = true;
                }
                if (after_dc < tokens_.size() && tokens_[after_dc].type == TokenType::LParen) {
                    std::size_t scan = after_dc + 1;
                    int paren_depth = 1;
                    while (scan < tokens_.size() && paren_depth > 0) {
                        if (tokens_[scan].type == TokenType::LParen) paren_depth++;
                        else if (tokens_[scan].type == TokenType::RParen) paren_depth--;
                        scan++;
                    }
                    if (scan < tokens_.size() && tokens_[scan].type == TokenType::Arrow) is_function = true;
                    if (scan < tokens_.size() && tokens_[scan].type == TokenType::Bang &&
                        scan + 1 < tokens_.size() && tokens_[scan + 1].type == TokenType::Arrow) is_function = true;
                }
            }

            pos_ = saved;
            if (is_function) {
                auto d = parse_function_decl();
                d->is_exported = exported;
                return d;
            }
            if (is_struct) { auto d = parse_struct_decl(); d->is_exported = exported; return d; }
            if (is_enum) { auto d = parse_enum_decl(); d->is_exported = exported; return d; }
            if (is_union) { auto d = parse_union_decl(); d->is_exported = exported; return d; }
            auto d = parse_constant_decl();
            d->is_exported = exported;
            return d;
        }
        pos_ = saved;
    }

    // variable declaration: type name [= expr];
    if (is_type_start()) {
        auto d = parse_variable_decl();
        d->is_exported = exported;
        return d;
    }

    error("unexpected token '" + peek().lexeme + "'");
    synchronize();
    return nullptr;
}

DeclPtr Parser::parse_variable_decl() {
    bool is_const = match(TokenType::KW_const);
    auto type = parse_type();
    auto name = parse_name();
    auto span = type->span;
    ExprPtr init;
    if (match(TokenType::Eq)) init = parse_expr();
    match(TokenType::Semicolon);
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::VariableDecl{name, std::move(type), std::move(init), is_const};
    return decl;
}

DeclPtr Parser::parse_constant_decl() {
    auto name = parse_name();
    auto span = peek().span;
    expect(TokenType::DoubleColon, "'::'");
    ExprPtr init = parse_expr();
    match(TokenType::Semicolon);
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::ConstantDecl{name, nullptr, std::move(init)};
    return decl;
}

DeclPtr Parser::parse_function_decl() {
    auto name = parse_name();
    auto span = peek().span;
    expect(TokenType::DoubleColon, "'::'");

    std::vector<TypePtr> generic_params;
    if (match(TokenType::Lt)) {
        while (!is_at_end() && !check(TokenType::Gt)) {
            generic_params.push_back(parse_type());
            match(TokenType::Comma);
        }
        expect(TokenType::Gt, "'>'");
    }

    expect(TokenType::LParen, "'('");
    std::vector<ast::Param> params;
    while (!is_at_end() && !check(TokenType::RParen)) {
        ast::Param p;
        p.type = parse_type();
        p.name = parse_name();
        p.name_span = peek().span;
        if (match(TokenType::Eq)) p.default_value = parse_expr();
        params.push_back(std::move(p));
        match(TokenType::Comma);
    }
    expect(TokenType::RParen, "')'");

    bool is_error_return = false;
    TypePtr return_type;
    if (match(TokenType::Arrow)) {
        if (match(TokenType::Bang)) is_error_return = true;
        return_type = parse_type();
    }

    StmtPtr body;
    if (!check(TokenType::KW_extern) && !is_at_end()) body = parse_block();

    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::FunctionDecl{name, std::move(generic_params), std::move(params),
                                    std::move(return_type), is_error_return, std::move(body),
                                    false, ""};
    return decl;
}

DeclPtr Parser::parse_struct_decl() {
    auto span = peek().span;
    std::string name;
    // Support both: "struct Name { }" and "Name :: struct { }"
    if (check(TokenType::Identifier) && peek_next().type == TokenType::DoubleColon) {
        // Already consumed from parse_decl: current pos is at the name
        name = parse_name();
        expect(TokenType::DoubleColon, "'::'");
        expect(TokenType::KW_struct, "'struct'");
    } else {
        advance(); // 'struct'
        name = parse_name();
    }
    span = peek().span;

    std::vector<std::string> generic_params;
    if (match(TokenType::Lt)) {
        while (!is_at_end() && !check(TokenType::Gt)) {
            generic_params.push_back(parse_name());
            match(TokenType::Comma);
        }
        expect(TokenType::Gt, "'>'");
    }

    expect(TokenType::LBrace, "'{'");
    std::vector<ast::StructField> fields;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        ast::StructField f;
        f.type = parse_type();
        f.name = parse_name();
        if (match(TokenType::Eq)) f.default_value = parse_expr();
        match(TokenType::Semicolon);
        fields.push_back(std::move(f));
    }
    expect(TokenType::RBrace, "'}'");

    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::StructDecl{name, std::move(generic_params), std::move(fields)};
    return decl;
}

DeclPtr Parser::parse_enum_decl() {
    auto span = peek().span;
    std::string name;
    if (check(TokenType::Identifier) && peek_next().type == TokenType::DoubleColon) {
        name = parse_name();
        expect(TokenType::DoubleColon, "'::'");
        expect(TokenType::KW_enum, "'enum'");
    } else {
        advance(); // 'enum'
        name = parse_name();
    }
    span = peek().span;
    expect(TokenType::Arrow, "'->'");
    auto backing_type = parse_type();
    expect(TokenType::LBrace, "'{'");
    std::vector<ast::EnumVariant> variants;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        ast::EnumVariant v;
        v.name = parse_name();
        if (match(TokenType::Eq)) v.value = parse_expr();
        variants.push_back(std::move(v));
        match(TokenType::Comma);
    }
    expect(TokenType::RBrace, "'}'");
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::EnumDecl{name, std::move(backing_type), std::move(variants)};
    return decl;
}

DeclPtr Parser::parse_union_decl() {
    auto span = peek().span;
    std::string name;
    if (check(TokenType::Identifier) && peek_next().type == TokenType::DoubleColon) {
        name = parse_name();
        expect(TokenType::DoubleColon, "'::'");
        expect(TokenType::KW_union, "'union'");
    } else {
        advance(); // 'union'
        name = parse_name();
    }
    span = peek().span;
    expect(TokenType::LBrace, "'{'");
    std::vector<ast::UnionField> fields;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        ast::UnionField f;
        f.type = parse_type();
        f.name = parse_name();
        match(TokenType::Semicolon);
        fields.push_back(std::move(f));
    }
    expect(TokenType::RBrace, "'}'");
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::UnionDecl{name, std::move(fields)};
    return decl;
}

DeclPtr Parser::parse_namespace_decl() {
    advance(); // 'namespace'
    auto name = parse_name();
    auto span = peek().span;
    expect(TokenType::LBrace, "'{'");
    std::vector<DeclPtr> decls;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        auto d = parse_decl();
        if (d) decls.push_back(std::move(d));
    }
    expect(TokenType::RBrace, "'}'");
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::NamespaceDecl{name, std::move(decls)};
    return decl;
}

DeclPtr Parser::parse_extern_block() {
    advance(); // 'extern'
    auto span = peek().span;
    expect(TokenType::StringLiteral, "ABI string");
    std::string abi = std::get<std::string>(tokens_[pos_ - 1].value);
    expect(TokenType::LBrace, "'{'");
    std::vector<DeclPtr> decls;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        auto d = parse_decl();
        if (d) {
            if (auto* func = std::get_if<ast::FunctionDecl>(&d->data)) {
                func->is_extern = true;
                func->extern_abi = abi;
            }
            decls.push_back(std::move(d));
        }
    }
    expect(TokenType::RBrace, "'}'");
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::ExternBlock{abi, std::move(decls)};
    return decl;
}

DeclPtr Parser::parse_import_decl() {
    advance(); // 'import'
    auto span = peek().span;
    std::string path = parse_name();
    while (match(TokenType::DoubleColon)) path += "::" + parse_name();
    std::optional<std::string> alias;
    if (match(TokenType::Identifier) && tokens_[pos_ - 1].lexeme == "as") alias = parse_name();
    match(TokenType::Semicolon);
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::ImportDecl{path, std::move(alias)};
    return decl;
}

DeclPtr Parser::parse_compile_time_if() {
    advance(); // #if
    auto span = peek().span;
    expect(TokenType::LParen, "'('");
    auto condition = parse_expr();
    expect(TokenType::RParen, "')'");
    expect(TokenType::LBrace, "'{'");
    std::vector<DeclPtr> then_decls;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        auto d = parse_decl();
        if (d) then_decls.push_back(std::move(d));
    }
    expect(TokenType::RBrace, "'}'");
    std::vector<DeclPtr> else_decls;
    if (match(TokenType::HashElse)) {
        expect(TokenType::LBrace, "'{'");
        while (!is_at_end() && !check(TokenType::RBrace)) {
            auto d = parse_decl();
            if (d) else_decls.push_back(std::move(d));
        }
        expect(TokenType::RBrace, "'}'");
    }
    auto decl = std::make_unique<ast::Decl>();
    decl->span = span;
    decl->data = ast::CompileTimeIf{std::move(condition), std::move(then_decls), std::move(else_decls)};
    return decl;
}

// ---- Types ----

TypePtr Parser::parse_type() {
    auto type = parse_basic_type();
    while (!is_at_end()) {
        if (match(TokenType::Star)) { type = parse_pointer_type(std::move(type)); }
        else if (check(TokenType::LBracket)) {
            advance();
            if (check(TokenType::RBracket)) { advance(); type = parse_slice_type(std::move(type)); }
            else { auto sz = parse_expr(); expect(TokenType::RBracket, "']'"); type = parse_array_type(std::move(type)); }
        } else break;
    }
    return type;
}

TypePtr Parser::parse_basic_type() {
    auto span = peek().span;
    if (is_builtin_type_token() || check(TokenType::KW_void)) {
        advance();
        auto t = std::make_unique<ast::TypeNode>();
        t->span = span;
        t->data = ast::PrimitiveType{tokens_[pos_ - 1].type};
        return t;
    }
    if (check(TokenType::LParen)) return parse_function_type();
    if (check(TokenType::Identifier) || is_builtin_type_token()) {
        auto name = parse_name();
        if (match(TokenType::Lt)) {
            std::vector<TypePtr> type_args;
            while (!is_at_end() && !check(TokenType::Gt)) {
                std::size_t before = pos_;
                type_args.push_back(parse_type());
                if (pos_ == before) advance(); // prevent infinite loop
                match(TokenType::Comma);
            }
            expect(TokenType::Gt, "'>'");
            auto t = std::make_unique<ast::TypeNode>();
            t->span = span;
            t->data = ast::GenericType{name, std::move(type_args), span};
            return t;
        }
        auto t = std::make_unique<ast::TypeNode>();
        t->span = span;
        t->data = ast::NamedType{name, span};
        return t;
    }
    if (match(TokenType::Bang)) {
        auto inner = parse_type();
        auto t = std::make_unique<ast::TypeNode>();
        t->span = span;
        ast::FunctionType ft;
        ft.return_type = std::move(inner);
        ft.is_error_return = true;
        t->data = std::move(ft);
        return t;
    }
    error("expected type, got '" + peek().lexeme + "'");
    auto t = std::make_unique<ast::TypeNode>();
    t->span = span;
    t->data = ast::NamedType{"error", span};
    return t;
}

TypePtr Parser::parse_function_type() {
    auto span = peek().span;
    expect(TokenType::LParen, "'('");
    std::vector<TypePtr> param_types;
    while (!is_at_end() && !check(TokenType::RParen)) {
        param_types.push_back(parse_type());
        match(TokenType::Comma);
    }
    expect(TokenType::RParen, "')'");
    bool is_error = false;
    expect(TokenType::Arrow, "'->'");
    if (match(TokenType::Bang)) is_error = true;
    auto return_type = parse_type();
    auto t = std::make_unique<ast::TypeNode>();
    t->span = span;
    ast::FunctionType ft;
    ft.param_types = std::move(param_types);
    ft.return_type = std::move(return_type);
    ft.is_error_return = is_error;
    t->data = std::move(ft);
    return t;
}

TypePtr Parser::parse_pointer_type(TypePtr inner) {
    auto t = std::make_unique<ast::TypeNode>();
    t->span = inner->span;
    t->data = ast::PointerType{std::move(inner)};
    return t;
}

TypePtr Parser::parse_slice_type(TypePtr inner) {
    auto t = std::make_unique<ast::TypeNode>();
    t->span = inner->span;
    t->data = ast::SliceType{std::move(inner)};
    return t;
}

TypePtr Parser::parse_array_type(TypePtr inner) {
    auto t = std::make_unique<ast::TypeNode>();
    t->span = inner->span;
    t->data = ast::ArrayType{std::move(inner), nullptr};
    return t;
}

// ---- Expressions ----

ExprPtr Parser::parse_expr() { return parse_or(); }

ExprPtr Parser::parse_or() {
    auto left = parse_and();
    while (match(TokenType::PipePipe)) {
        auto right = parse_and();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{ast::BinaryExpr::LogicOr, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    auto left = parse_bitwise_or();
    while (match(TokenType::AmpAmp)) {
        auto right = parse_bitwise_or();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{ast::BinaryExpr::LogicAnd, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_bitwise_or() {
    auto left = parse_bitwise_xor();
    while (match(TokenType::Pipe)) {
        auto right = parse_bitwise_xor();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{ast::BinaryExpr::BitOr, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_bitwise_xor() {
    auto left = parse_bitwise_and();
    while (match(TokenType::Caret)) {
        auto right = parse_bitwise_and();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{ast::BinaryExpr::BitXor, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_bitwise_and() {
    auto left = parse_equality();
    while (match(TokenType::Amp)) {
        auto right = parse_equality();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{ast::BinaryExpr::BitAnd, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_equality() {
    auto left = parse_comparison();
    while (check(TokenType::EqEq) || check(TokenType::Neq)) {
        auto op = advance();
        auto right = parse_comparison();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{op.type == TokenType::EqEq ? ast::BinaryExpr::Eq : ast::BinaryExpr::Neq,
                                   std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_comparison() {
    auto left = parse_shift();
    while (check(TokenType::Lt) || check(TokenType::Gt) || check(TokenType::Le) || check(TokenType::Ge)) {
        auto op = advance();
        auto right = parse_shift();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        ast::BinaryExpr::Op bop;
        switch (op.type) {
            case TokenType::Lt: bop = ast::BinaryExpr::Lt; break;
            case TokenType::Gt: bop = ast::BinaryExpr::Gt; break;
            case TokenType::Le: bop = ast::BinaryExpr::Le; break;
            default: bop = ast::BinaryExpr::Ge; break;
        }
        e->data = ast::BinaryExpr{bop, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_shift() {
    auto left = parse_additive();
    while (check(TokenType::LShift) || check(TokenType::RShift)) {
        auto op = advance();
        auto right = parse_additive();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{op.type == TokenType::LShift ? ast::BinaryExpr::LShift : ast::BinaryExpr::RShift,
                                   std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_additive() {
    auto left = parse_multiplicative();
    while (check(TokenType::Plus) || check(TokenType::Minus)) {
        auto op = advance();
        auto right = parse_multiplicative();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        e->data = ast::BinaryExpr{op.type == TokenType::Plus ? ast::BinaryExpr::Add : ast::BinaryExpr::Sub,
                                   std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_multiplicative() {
    auto left = parse_unary();
    while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
        auto op = advance();
        auto right = parse_unary();
        auto e = std::make_unique<ast::Expr>();
        e->span = left->span;
        ast::BinaryExpr::Op bop;
        switch (op.type) {
            case TokenType::Star: bop = ast::BinaryExpr::Mul; break;
            case TokenType::Slash: bop = ast::BinaryExpr::Div; break;
            default: bop = ast::BinaryExpr::Mod; break;
        }
        e->data = ast::BinaryExpr{bop, std::move(left), std::move(right)};
        left = std::move(e);
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    auto span = peek().span;
    if (match(TokenType::Minus)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::Neg, std::move(op)}; return e;
    }
    if (match(TokenType::Bang)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::Not, std::move(op)}; return e;
    }
    if (match(TokenType::Tilde)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::BitNot, std::move(op)}; return e;
    }
    if (match(TokenType::Star)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::Deref, std::move(op)}; return e;
    }
    if (match(TokenType::Amp)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::Addr, std::move(op)}; return e;
    }
    if (match(TokenType::PlusPlus)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::PreInc, std::move(op)}; return e;
    }
    if (match(TokenType::MinusMinus)) {
        auto op = parse_unary();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::UnaryExpr{ast::UnaryExpr::PreDec, std::move(op)}; return e;
    }
    // Cast: Type(expr) or Type!(expr)
    if ((check(TokenType::Identifier) || is_builtin_type_token()) && peek_next().type == TokenType::LParen) {
        // Peek ahead to check if this is a cast: "identifier(" could be a function call
        // Only treat as cast if the identifier is a known type name
        auto saved = pos_;
        auto type = parse_type();
        if (type && match(TokenType::LParen)) {
            auto value = parse_expr();
            expect(TokenType::RParen, "')'");
            auto e = std::make_unique<ast::Expr>(); e->span = span;
            ast::CastExpr cast{std::move(type), std::move(value), false};
            e->data = std::move(cast); return e;
        }
        pos_ = saved;
    }
    if (match(TokenType::KW_success)) {
        expect(TokenType::LParen, "'('");
        auto value = parse_expr();
        expect(TokenType::RParen, "')'");
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::SuccessExpr{std::move(value)}; return e;
    }
    if (match(TokenType::KW_failure)) {
        std::optional<ExprPtr> value;
        if (match(TokenType::LParen)) {
            if (!check(TokenType::RParen)) value = parse_expr();
            expect(TokenType::RParen, "')'");
        }
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::FailureExpr{std::move(value)}; return e;
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    auto expr = parse_primary();
    while (!is_at_end()) {
        if (match(TokenType::PlusPlus)) {
            auto p = std::make_unique<ast::Expr>(); p->span = expr->span;
            p->data = ast::PostfixExpr{ast::PostfixExpr::Inc, std::move(expr)}; expr = std::move(p);
        } else if (match(TokenType::MinusMinus)) {
            auto p = std::make_unique<ast::Expr>(); p->span = expr->span;
            p->data = ast::PostfixExpr{ast::PostfixExpr::Dec, std::move(expr)}; expr = std::move(p);
        } else if (check(TokenType::LParen)) {
            // Function call
            advance();
            std::vector<ExprPtr> args;
            while (!is_at_end() && !check(TokenType::RParen)) {
                args.push_back(parse_expr());
                match(TokenType::Comma);
            }
            expect(TokenType::RParen, "')'");
            auto c = std::make_unique<ast::Expr>(); c->span = expr->span;
            c->data = ast::CallExpr{std::move(expr), std::move(args)}; expr = std::move(c);
        } else if (match(TokenType::LBracket)) {
            auto idx = parse_expr();
            expect(TokenType::RBracket, "']'");
            auto i = std::make_unique<ast::Expr>(); i->span = expr->span;
            i->data = ast::IndexExpr{std::move(expr), std::move(idx)}; expr = std::move(i);
        } else if (match(TokenType::Dot)) {
            std::string member;
            if (check(TokenType::Identifier)) member = advance().lexeme;
            else if (is_builtin_type_token()) member = advance().lexeme;
            else member = parse_name();
            auto m = std::make_unique<ast::Expr>(); m->span = expr->span;
            m->data = ast::MemberExpr{std::move(expr), member, peek().span}; expr = std::move(m);
        } else if (match(TokenType::DoubleColon)) {
            std::string member;
            if (check(TokenType::Identifier)) member = advance().lexeme;
            else if (is_builtin_type_token()) member = advance().lexeme;
            else member = parse_name();
            auto m = std::make_unique<ast::Expr>(); m->span = expr->span;
            m->data = ast::MemberExpr{std::move(expr), member, peek().span}; expr = std::move(m);
        } else if (match(TokenType::Arrow)) {
            auto member = parse_name();
            auto deref = std::make_unique<ast::Expr>(); deref->span = expr->span;
            deref->data = ast::UnaryExpr{ast::UnaryExpr::Deref, std::move(expr)};
            auto m = std::make_unique<ast::Expr>(); m->span = deref->span;
            m->data = ast::MemberExpr{std::move(deref), member, peek().span}; expr = std::move(m);
        } else if (check(TokenType::DoubleQuestion)) {
            advance();
            // Bare `??` propagates: expr?? ;  (next is ; or } or similar)
            // Branch form: expr ?? on_success : on_failure
            if (check(TokenType::Semicolon) || check(TokenType::RBrace) || check(TokenType::RParen)) {
                auto b = std::make_unique<ast::Expr>(); b->span = expr->span;
                b->data = ast::ResultBranchExpr{std::move(expr), nullptr, nullptr};
                expr = std::move(b);
            }
            // Branch form: expr ?? (type name) { body } : (type name) { body }
            else if (check(TokenType::LParen)) {
                auto on_success = parse_branch_arm();
                expect(TokenType::Colon, "':'");
                auto on_failure = parse_branch_arm();
                auto b = std::make_unique<ast::Expr>(); b->span = expr->span;
                b->data = ast::ResultBranchExpr{std::move(expr), std::move(on_success), std::move(on_failure)};
                expr = std::move(b);
            }
            // Legacy branch form: expr ?? primary : primary
            else {
                auto on_success = parse_primary();
                expect(TokenType::Colon, "':'");
                auto on_failure = parse_primary();
                auto b = std::make_unique<ast::Expr>(); b->span = expr->span;
                b->data = ast::ResultBranchExpr{std::move(expr), std::move(on_success), std::move(on_failure)};
                expr = std::move(b);
            }
        } else if (check(TokenType::Lt) && std::holds_alternative<ast::Identifier>(expr->data)) {
            // Speculative lookahead: distinguish generic<T> from comparison < expr
            std::size_t saved = pos_;
            advance(); // past '<'
            bool looks_generic = false;
            std::size_t scan = pos_;
            int depth = 1;
            while (scan < tokens_.size() && depth > 0) {
                auto tt = tokens_[scan].type;
                if (tt == TokenType::Lt) depth++;
                else if (tt == TokenType::Gt) depth--;
                else if (tt == TokenType::Semicolon || tt == TokenType::LBrace ||
                         tt == TokenType::RBrace || tt == TokenType::Eq ||
                         tt == TokenType::Plus || tt == TokenType::Minus ||
                         tt == TokenType::Star || tt == TokenType::Slash ||
                         tt == TokenType::Percent || tt == TokenType::Amp ||
                         tt == TokenType::Pipe || tt == TokenType::Caret ||
                         tt == TokenType::RParen || tt == TokenType::Comma ||
                         tt == TokenType::EqEq || tt == TokenType::Neq ||
                         tt == TokenType::Le || tt == TokenType::Ge) {
                    break;
                }
                scan++;
            }
            if (depth == 0) looks_generic = true;
            pos_ = saved; // restore

            if (looks_generic) {
                advance(); // past '<'
                std::vector<TypePtr> type_args;
                while (!is_at_end() && !check(TokenType::Gt)) { type_args.push_back(parse_type()); match(TokenType::Comma); }
                expect(TokenType::Gt, "'>'");
                if (check(TokenType::LParen)) {
                    advance();
                    std::vector<ExprPtr> args;
                    while (!is_at_end() && !check(TokenType::RParen)) { args.push_back(parse_expr()); match(TokenType::Comma); }
                    expect(TokenType::RParen, "')'");
                }
                auto g = std::make_unique<ast::Expr>(); g->span = expr->span;
                g->data = ast::GenericCallExpr{std::move(expr), std::move(type_args)}; expr = std::move(g);
            } else {
                break; // '<' is a comparison operator
            }
        } else break;
    }
    return expr;
}

// Parse a ?? branch arm: (type name) { body }
ExprPtr Parser::parse_branch_arm() {
    expect(TokenType::LParen, "'('");
    parse_type();
    parse_name();
    expect(TokenType::RParen, "')'");
    parse_block();
    auto e = std::make_unique<ast::Expr>();
    e->span = {};
    e->data = ast::NullLit{};
    return e;
}

ExprPtr Parser::parse_primary() {
    auto span = peek().span;

    if (match(TokenType::IntegerLiteral)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::IntegerLit{std::get<std::int64_t>(tokens_[pos_ - 1].value)}; return e;
    }
    if (match(TokenType::FloatLiteral)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::FloatLit{std::get<double>(tokens_[pos_ - 1].value)}; return e;
    }
    if (match(TokenType::CharLiteral)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::CharLit{std::get<std::string>(tokens_[pos_ - 1].value)}; return e;
    }
    if (match(TokenType::StringLiteral)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::StringLit{std::get<std::string>(tokens_[pos_ - 1].value)}; return e;
    }
    if (check(TokenType::BoolLiteral)) {
        bool val = std::get<std::int64_t>(peek().value) != 0; advance();
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::BoolLit{val}; return e;
    }
    if (match(TokenType::KW_null)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::NullLit{}; return e;
    }
    // @-builtins
    if (check(TokenType::Identifier) && peek().lexeme.starts_with("@")) {
        auto name = advance().lexeme;
        if (name == "@sizeof" || name == "@alignof") {
            expect(TokenType::LParen, "'('");
            auto type = parse_type();
            expect(TokenType::RParen, "')'");
            auto e = std::make_unique<ast::Expr>(); e->span = span;
            if (name == "@sizeof") e->data = ast::SizeofExpr{std::move(type)};
            else e->data = ast::AlignofExpr{std::move(type)};
            return e;
        }
        if (name == "@typeof") {
            expect(TokenType::LParen, "'('");
            auto inner = parse_expr();
            expect(TokenType::RParen, "')'");
            auto e = std::make_unique<ast::Expr>(); e->span = span;
            e->data = ast::TypeofExpr{std::move(inner)}; return e;
        }
        if (name == "@bitcast") {
            expect(TokenType::LParen, "'('");
            auto type = parse_type();
            expect(TokenType::Comma, "','");
            auto value = parse_expr();
            expect(TokenType::RParen, "')'");
            auto e = std::make_unique<ast::Expr>(); e->span = span;
            e->data = ast::BitcastExpr{std::move(type), std::move(value)}; return e;
        }
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::BuiltinExpr{name, span}; return e;
    }
    if (match(TokenType::Identifier)) {
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::Identifier{tokens_[pos_ - 1].lexeme, span}; return e;
    }
    if (match(TokenType::LParen)) {
        auto inner = parse_expr();
        expect(TokenType::RParen, "')'");
        return inner;
    }
    if (match(TokenType::LBracket)) {
        std::vector<ExprPtr> elements;
        while (!is_at_end() && !check(TokenType::RBracket)) {
            elements.push_back(parse_expr());
            match(TokenType::Comma);
        }
        expect(TokenType::RBracket, "']'");
        auto e = std::make_unique<ast::Expr>(); e->span = span;
        e->data = ast::ArrayLiteral{std::move(elements)}; return e;
    }
    error("unexpected token '" + peek().lexeme + "'");
    auto e = std::make_unique<ast::Expr>(); e->span = span;
    e->data = ast::NullLit{}; return e;
}

// ---- Statements ----

StmtPtr Parser::parse_stmt() {
    auto span = peek().span;
    if (check(TokenType::LBrace)) return parse_block();
    if (check(TokenType::KW_if)) return parse_if_stmt();
    if (check(TokenType::KW_while)) return parse_while_stmt();
    if (check(TokenType::KW_do)) return parse_do_while_stmt();
    if (check(TokenType::KW_switch)) return parse_switch_stmt();
    if (check(TokenType::KW_foreach)) return parse_foreach_stmt();
    if (check(TokenType::KW_return)) return parse_return_stmt();
    if (check(TokenType::KW_break)) return parse_break_stmt();
    if (check(TokenType::KW_continue)) return parse_continue_stmt();

    // Variable declaration: type name [= expr];
    if (is_type_start()) {
        auto type = parse_type();
        auto name = parse_name();
        ExprPtr init;
        if (match(TokenType::Eq)) init = parse_expr();
        match(TokenType::Semicolon);
        auto s = std::make_unique<ast::Stmt>(); s->span = span;
        auto d = std::make_unique<ast::Decl>();
        d->span = span;
        d->data = ast::VariableDecl{name, std::move(type), std::move(init), false};
        s->data = std::move(d);
        return s;
    }

    // Expression statement (with optional assignment)
    auto expr = parse_expr();
    if (match(TokenType::Eq)) {
        auto value = parse_expr();
        match(TokenType::Semicolon);
        auto s = std::make_unique<ast::Stmt>(); s->span = span;
        s->data = ast::AssignStmt{std::move(expr), std::move(value)}; return s;
    }
    match(TokenType::Semicolon);
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::ExprStmt{std::move(expr)}; return s;
}

StmtPtr Parser::parse_block() {
    auto span = peek().span;
    expect(TokenType::LBrace, "'{'");
    Block block;
    while (!is_at_end() && !check(TokenType::RBrace)) block.stmts.push_back(parse_stmt());
    expect(TokenType::RBrace, "'}'");
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = std::move(block); return s;
}

StmtPtr Parser::parse_if_stmt() {
    auto span = peek().span;
    advance(); // if
    auto condition = parse_expr();
    expect(TokenType::KW_then, "'then'");
    auto then_block = parse_block();

    // Build the if-else-if chain iteratively to avoid stack overflow
    // We'll construct a linked list of IfStmt nodes where each else_block
    // points to the next IfStmt (for else if) or a Block (for final else)
    struct IfChainNode {
        ExprPtr condition;
        StmtPtr then_block;
        std::optional<StmtPtr> else_block; // Will be filled in next iteration
    };
    std::vector<IfChainNode> chain;
    chain.push_back({std::move(condition), std::move(then_block), std::nullopt});

    // Parse all else-if branches iteratively
    while (match(TokenType::KW_else)) {
        if (check(TokenType::KW_if)) {
            advance(); // consume 'if'
            auto elif_cond = parse_expr();
            expect(TokenType::KW_then, "'then'");
            auto elif_then = parse_block();
            chain.push_back({std::move(elif_cond), std::move(elif_then), std::nullopt});
        } else {
            // Final else block
            auto else_blk = parse_block();
            chain.back().else_block = std::move(else_blk);
            break;
        }
    }

    // Now build the AST from the chain, starting from the last node
    std::optional<StmtPtr> next_else;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        auto s = std::make_unique<ast::Stmt>();
        s->span = it->condition->span; // Use condition span for the IfStmt
        // Use the else_block from the chain node if present, otherwise use next_else
        std::optional<StmtPtr> else_branch = it->else_block.has_value() 
            ? std::move(it->else_block) 
            : std::move(next_else);
        s->data = ast::IfStmt{
            std::move(it->condition),
            std::move(it->then_block),
            std::move(else_branch)
        };
        next_else = std::move(s);
    }

    // The first node in the chain is the original if statement
    // Its span should be the original 'if' token span
    if (next_else.has_value()) {
        (*next_else)->span = span;
    }
    return std::move(next_else).value();
}

StmtPtr Parser::parse_while_stmt() {
    auto span = peek().span;
    advance();
    auto condition = parse_expr();
    auto body = parse_block();
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::WhileStmt{std::move(condition), std::move(body)}; return s;
}

StmtPtr Parser::parse_do_while_stmt() {
    auto span = peek().span;
    advance();
    auto body = parse_block();
    expect(TokenType::KW_while, "'while'");
    auto condition = parse_expr();
    match(TokenType::Semicolon);
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::DoWhileStmt{std::move(body), std::move(condition)}; return s;
}

StmtPtr Parser::parse_switch_stmt() {
    auto span = peek().span;
    advance();
    auto subject = parse_expr();
    expect(TokenType::LBrace, "'{'");
    std::vector<ast::CaseArm> cases;
    std::optional<StmtPtr> default_case;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        if (match(TokenType::KW_default)) {
            default_case = parse_block();
        } else if (match(TokenType::KW_case)) {
            auto cond = parse_expr();
            auto body = parse_block();
            cases.push_back(ast::CaseArm{std::move(cond), std::move(body)});
        } else { error("expected 'case' or 'default'"); advance(); }
    }
    expect(TokenType::RBrace, "'}'");
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::SwitchStmt{std::move(subject), std::move(cases), std::move(default_case)};
    return s;
}

StmtPtr Parser::parse_match_stmt() {
    auto span = peek().span;
    advance();
    expect(TokenType::LParen, "'('");
    auto subject = parse_expr();
    expect(TokenType::RParen, "')'");
    expect(TokenType::LBrace, "'{'");
    std::vector<ast::MatchArm> arms;
    while (!is_at_end() && !check(TokenType::RBrace)) {
        auto cond = parse_expr();
        expect(TokenType::LBrace, "'{'");
        auto value = parse_expr();
        expect(TokenType::RBrace, "'}'");
        arms.push_back(ast::MatchArm{std::move(cond), std::move(value)});
    }
    expect(TokenType::RBrace, "'}'");
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::MatchStmt{std::move(subject), std::move(arms)}; return s;
}

StmtPtr Parser::parse_foreach_stmt() {
    auto span = peek().span;
    advance();
    expect(TokenType::LParen, "'('");
    auto elem_type = parse_type();
    auto elem_name = parse_name();
    std::optional<std::string> index_var;
    if (match(TokenType::Comma)) {
        index_var = elem_name;
        elem_type = parse_type();
        elem_name = parse_name();
    }
    expect(TokenType::KW_in, "'in'");
    auto iterable = parse_expr();
    expect(TokenType::RParen, "')'");
    auto body = parse_block();
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::ForeachStmt{std::move(index_var), elem_name, std::move(elem_type),
                                std::move(iterable), std::move(body)}; return s;
}

StmtPtr Parser::parse_return_stmt() {
    auto span = peek().span;
    advance();
    std::optional<ExprPtr> value;
    if (!check(TokenType::Semicolon) && !check(TokenType::RBrace)) value = parse_expr();
    match(TokenType::Semicolon);
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::ReturnStmt{std::move(value)}; return s;
}

StmtPtr Parser::parse_break_stmt() {
    auto span = peek().span;
    advance();
    std::uint32_t levels = 1;
    if (match(TokenType::LParen)) {
        if (check(TokenType::IntegerLiteral))
            levels = static_cast<std::uint32_t>(std::get<std::int64_t>(advance().value));
        expect(TokenType::RParen, "')'");
    }
    match(TokenType::Semicolon);
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::BreakStmt{levels}; return s;
}

StmtPtr Parser::parse_continue_stmt() {
    auto span = peek().span;
    advance();
    std::uint32_t levels = 1;
    if (match(TokenType::LParen)) {
        if (check(TokenType::IntegerLiteral))
            levels = static_cast<std::uint32_t>(std::get<std::int64_t>(advance().value));
        expect(TokenType::RParen, "')'");
    }
    match(TokenType::Semicolon);
    auto s = std::make_unique<ast::Stmt>(); s->span = span;
    s->data = ast::ContinueStmt{levels}; return s;
}

} // namespace femto
