#include "frontend/parser.hpp"

namespace femto {

int Parser::get_binary_precedence(TokenKind kind) {
    switch (kind) {
        case TokenKind::PipePipe: return 2;
        case TokenKind::AmpAmp:   return 3;
        case TokenKind::Pipe:      return 4;
        case TokenKind::Caret:     return 5;
        case TokenKind::Amp:       return 6;
        case TokenKind::EqEq:
        case TokenKind::BangEq:    return 7;
        case TokenKind::Lt:
        case TokenKind::LtEq:
        case TokenKind::Gt:
        case TokenKind::GtEq:      return 8;
        case TokenKind::Shl:
        case TokenKind::Shr:       return 9;
        case TokenKind::Plus:
        case TokenKind::Minus:     return 10;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:   return 11;
        default: return 0;
    }
}

ASTType* Parser::parse_type() {
    SourceSpan start_span = current_.span;
    if (match(TokenKind::Bang)) {
        ASTType* ret = arena_.allocate<ASTType>();
        ret->kind = TypeKind::Result;
        ret->span = start_span;
        if (current_.kind == TokenKind::KwVoid) {
            advance();
            ret->pointee_or_element = nullptr;
        } else {
            ret->pointee_or_element = parse_type();
        }
        return ret;
    }

    ASTType* base_ty = nullptr;

    if (current_.kind >= TokenKind::KwInt8 && current_.kind <= TokenKind::KwString32) {
        Token t = current_;
        advance();
        base_ty = arena_.allocate<ASTType>();
        base_ty->kind = TypeKind::Primitive;
        base_ty->primitive_kind = t.kind;
        base_ty->span = t.span;
    } else if (current_.kind == TokenKind::Identifier) {
        Token id = current_;
        advance();
        base_ty = arena_.allocate<ASTType>();
        base_ty->kind = TypeKind::Custom;
        base_ty->custom_name = id.text;
        base_ty->span = id.span;
    } else {
        diag_.report_error(current_.span, "expected type specifier");
        advance();
        return nullptr;
    }

    // Pointer suffix: Type*
    while (match(TokenKind::Star)) {
        ASTType* ptr_ty = arena_.allocate<ASTType>();
        ptr_ty->kind = TypeKind::Pointer;
        ptr_ty->pointee_or_element = base_ty;
        ptr_ty->span = base_ty->span.merge(current_.span);
        base_ty = ptr_ty;
    }

    // Array / Slice suffix: Type[N] or Type[]
    if (match(TokenKind::LBracket)) {
        if (match(TokenKind::RBracket)) {
            ASTType* slice = arena_.allocate<ASTType>();
            slice->kind = TypeKind::Slice;
            slice->pointee_or_element = base_ty;
            slice->primitive_kind = base_ty->primitive_kind;
            slice->span = start_span.merge(current_.span);
            base_ty = slice;
        } else {
            Token size_tok = expect(TokenKind::IntLiteral, "expected array size");
            expect(TokenKind::RBracket, "expected ']'");
            ASTType* arr = arena_.allocate<ASTType>();
            arr->kind = TypeKind::Array;
            arr->pointee_or_element = base_ty;
            arr->primitive_kind = base_ty->primitive_kind;
            arr->array_size = std::stoull(std::string(size_tok.text));
            arr->span = start_span.merge(current_.span);
            base_ty = arr;
        }
    }

    return base_ty;
}

ASTExpr* Parser::parse_primary_expression() {
    SourceSpan span = current_.span;

    // Unary operators: -, !, ~, +, &, *
    if (current_.kind == TokenKind::Minus || current_.kind == TokenKind::Bang || 
        current_.kind == TokenKind::Tilde || current_.kind == TokenKind::Plus ||
        current_.kind == TokenKind::Amp   || current_.kind == TokenKind::Star) {
        Token op_tok = current_;
        advance();
        ASTExpr* operand = parse_expression(12);
        ASTExpr* un = arena_.allocate<ASTExpr>();
        un->kind = ExprKind::Unary;
        un->op = op_tok.text;
        un->left = operand;
        un->span = span.merge(operand ? operand->span : op_tok.span);
        return un;
    }

    // Struct literal: { .x = 10, .y = 20 } or {}
    if (match(TokenKind::LBrace)) {
        ASTExpr* lit = arena_.allocate<ASTExpr>();
        lit->kind = ExprKind::StructLiteral;
        lit->span = span;

        while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
            if (match(TokenKind::Dot)) {
                Token f_name = expect(TokenKind::Identifier, "expected field name");
                expect(TokenKind::Eq, "expected '=' after field name");
                ASTExpr* f_val = parse_expression();
                lit->struct_fields.push_back({f_name.text, f_val});
                match(TokenKind::Comma);
            } else {
                diag_.report_error(current_.span, "expected '.field = expr' in struct literal");
                advance();
            }
        }
        expect(TokenKind::RBrace, "expected '}' closing struct literal");
        return lit;
    }

    // Array literal: [ 10, 20, 30 ]
    if (match(TokenKind::LBracket)) {
        ASTExpr* arr_lit = arena_.allocate<ASTExpr>();
        arr_lit->kind = ExprKind::ArrayLiteral;
        arr_lit->span = span;

        if (current_.kind != TokenKind::RBracket) {
            do {
                arr_lit->args.push_back(parse_expression());
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBracket, "expected ']' closing array literal");
        return arr_lit;
    }

    if (match(TokenKind::HashSubject)) {
        ASTExpr* e = arena_.allocate<ASTExpr>();
        e->kind = ExprKind::Subject;
        e->span = span;
        return e;
    }

    if (current_.kind == TokenKind::IntLiteral || current_.kind == TokenKind::FloatLiteral ||
        current_.kind == TokenKind::StringLiteral || current_.kind == TokenKind::RawStringLiteral ||
        current_.kind == TokenKind::CharLiteral || current_.kind == TokenKind::KwTrue || current_.kind == TokenKind::KwFalse) {
        Token t = current_;
        advance();
        ASTExpr* e = arena_.allocate<ASTExpr>();
        e->kind = ExprKind::Literal;
        e->raw_text = t.text;
        e->span = span;
        return e;
    }

    if (current_.kind == TokenKind::Identifier) {
        Token id = current_;
        advance();
        ASTExpr* e = arena_.allocate<ASTExpr>();
        e->kind = ExprKind::Identifier;
        e->raw_text = id.text;
        e->span = span;
        return e;
    }

    if (match(TokenKind::KwMatch)) {
        expect(TokenKind::LParen, "expected '(' after match");
        ASTExpr* subject = parse_expression();
        expect(TokenKind::RParen, "expected ')'");
        expect(TokenKind::LBrace, "expected '{' to start match arms");

        ASTExpr* m = arena_.allocate<ASTExpr>();
        m->kind = ExprKind::Match;
        m->left = subject;

        while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
            ASTMatchArm arm;
            arm.span = current_.span;
            if (match(TokenKind::KwDefault)) {
                arm.condition = nullptr;
            } else {
                arm.condition = parse_expression();
            }
            expect(TokenKind::LBrace, "expected '{' for arm body");
            while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
                arm.result_expr = parse_expression();
                break;
            }
            expect(TokenKind::RBrace, "expected '}' closing arm");
            m->match_arms.push_back(arm);
        }
        expect(TokenKind::RBrace, "expected '}' closing match block");
        return m;
    }

    if (match(TokenKind::LParen)) {
        ASTExpr* inner = parse_expression();
        expect(TokenKind::RParen, "expected ')'");
        return inner;
    }

    diag_.report_error(current_.span, "expected expression");
    advance();
    return nullptr;
}

ASTExpr* Parser::parse_expression(int min_precedence) {
    ASTExpr* left = parse_primary_expression();
    if (!left) return nullptr;

    while (true) {
        if (match(TokenKind::QuestionQuestion)) {
            ASTExpr* unwrap = arena_.allocate<ASTExpr>();
            unwrap->kind = ExprKind::PostfixUnwrap;
            unwrap->left = left;
            unwrap->span = left->span.merge(current_.span);
            left = unwrap;
            continue;
        }

        // Member access: expr.field or expr.length()
        if (match(TokenKind::Dot)) {
            Token field_tok = expect(TokenKind::Identifier, "expected member name after '.'");
            if (match(TokenKind::LParen)) {
                expect(TokenKind::RParen, "expected ')' after method call");
                ASTExpr* mem = arena_.allocate<ASTExpr>();
                mem->kind = ExprKind::MemberAccess;
                mem->left = left;
                mem->raw_text = field_tok.text;
                mem->op = "()";
                left = mem;
            } else {
                ASTExpr* mem = arena_.allocate<ASTExpr>();
                mem->kind = ExprKind::MemberAccess;
                mem->left = left;
                mem->raw_text = field_tok.text;
                left = mem;
            }
            continue;
        }

        // Subscript / Slicing: expr[i] or expr[start..end]
        if (match(TokenKind::LBracket)) {
            ASTExpr* index_or_start = parse_expression();
            if (match(TokenKind::DotDot)) {
                ASTExpr* end = parse_expression();
                expect(TokenKind::RBracket, "expected ']'");
                ASTExpr* slice = arena_.allocate<ASTExpr>();
                slice->kind = ExprKind::SliceSubrange;
                slice->left = left;
                slice->args = { index_or_start, end };
                left = slice;
            } else {
                expect(TokenKind::RBracket, "expected ']'");
                ASTExpr* idx = arena_.allocate<ASTExpr>();
                idx->kind = ExprKind::Index;
                idx->left = left;
                idx->right = index_or_start;
                left = idx;
            }
            continue;
        }

        if (match(TokenKind::LParen)) {
            ASTExpr* call = arena_.allocate<ASTExpr>();
            call->kind = ExprKind::Call;
            call->left = left;
            if (current_.kind != TokenKind::RParen) {
                do {
                    call->args.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')'");
            left = call;
            continue;
        }

        int prec = get_binary_precedence(current_.kind);
        if (prec < min_precedence || prec == 0) break;

        Token op_tok = current_;
        advance();

        ASTExpr* right = parse_expression(prec + 1);
        ASTExpr* bin = arena_.allocate<ASTExpr>();
        bin->kind = ExprKind::Binary;
        bin->left = left;
        bin->right = right;
        bin->op = op_tok.text;
        bin->span = left->span.merge(right ? right->span : op_tok.span);
        left = bin;
    }

    return left;
}

ASTProgram Parser::parse_program() {
    ASTProgram prog;
    while (current_.kind != TokenKind::Eof) {
        bool is_exported = false;
        if (match(TokenKind::HashExport)) {
            is_exported = true;
        }
        parse_top_level_declaration(prog, is_exported);
    }
    return prog;
}

void Parser::parse_top_level_declaration(ASTProgram& prog, bool is_exported) {
    if (current_.kind == TokenKind::Identifier) {
        Token name_tok = current_;
        advance();
        expect(TokenKind::DoubleColon, "expected '::' after identifier");

        if (match(TokenKind::KwStruct)) {
            prog.structs.push_back(parse_struct_decl(name_tok.text, is_exported));
        } else if (match(TokenKind::KwEnum)) {
            prog.enums.push_back(parse_enum_decl(name_tok.text, is_exported));
        } else if (current_.kind == TokenKind::LParen || current_.kind == TokenKind::Lt) {
            prog.functions.push_back(parse_function_decl(name_tok.text, is_exported));
        } else {
            diag_.report_error(current_.span, "unsupported declaration following '::'");
            advance();
        }
    } else {
        diag_.report_error(current_.span, "expected top-level declaration");
        advance();
    }
}

ASTFunctionDecl* Parser::parse_function_decl(std::string_view name, bool is_exported) {
    auto* fn = arena_.allocate<ASTFunctionDecl>();
    fn->name = name;
    fn->is_exported = is_exported;
    fn->span = current_.span;

    expect(TokenKind::LParen, "expected '(' for parameter list");
    if (current_.kind != TokenKind::RParen) {
        do {
            ASTParam p;
            p.type = parse_type();
            Token p_name = expect(TokenKind::Identifier, "expected parameter name");
            p.name = p_name.text;
            if (match(TokenKind::Eq)) {
                p.default_value = parse_expression();
            }
            fn->params.push_back(p);
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Arrow, "expected '->' specifying return type");
    fn->return_type = parse_type();
    fn->body = parse_block();
    return fn;
}

ASTStructDecl* Parser::parse_struct_decl(std::string_view name, bool is_exported) {
    auto* s = arena_.allocate<ASTStructDecl>();
    s->name = name;
    s->is_exported = is_exported;
    expect(TokenKind::LBrace, "expected '{' for struct body");
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        ASTStructField f;
        f.type = parse_type();
        Token f_name = expect(TokenKind::Identifier, "expected field name");
        f.name = f_name.text;
        if (match(TokenKind::Eq)) {
            f.default_value = parse_expression();
        }
        expect(TokenKind::Semicolon, "expected ';' after struct field");
        s->fields.push_back(f);
    }
    expect(TokenKind::RBrace, "expected '}' closing struct");
    return s;
}

ASTEnumDecl* Parser::parse_enum_decl(std::string_view name, bool is_exported) {
    auto* e = arena_.allocate<ASTEnumDecl>();
    e->name = name;
    e->is_exported = is_exported;
    expect(TokenKind::Arrow, "expected '->' specifying enum backing type");
    e->backing_type = parse_type();
    expect(TokenKind::LBrace, "expected '{' for enum body");
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        Token var_name = expect(TokenKind::Identifier, "expected enum variant name");
        ASTEnumVariant v;
        v.name = var_name.text;
        if (match(TokenKind::Eq)) {
            Token val_tok = expect(TokenKind::IntLiteral, "expected integer for enum variant");
            v.value = std::stoll(std::string(val_tok.text));
        }
        match(TokenKind::Comma);
        e->variants.push_back(v);
    }
    expect(TokenKind::RBrace, "expected '}' closing enum");
    return e;
}

std::vector<ASTStmt*> Parser::parse_block() {
    std::vector<ASTStmt*> stmts;
    expect(TokenKind::LBrace, "expected '{'");
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        stmts.push_back(parse_statement());
    }
    expect(TokenKind::RBrace, "expected '}'");
    return stmts;
}

ASTStmt* Parser::parse_statement() {
    if (match(TokenKind::KwIf)) return parse_if_stmt();
    if (match(TokenKind::KwWhile)) return parse_while_stmt();
    if (match(TokenKind::KwDo)) return parse_do_while_stmt();
    if (match(TokenKind::KwSwitch)) return parse_switch_stmt();
    if (match(TokenKind::KwForeach)) return parse_foreach_stmt();
    if (match(TokenKind::KwBreak)) return parse_break_stmt();
    if (match(TokenKind::KwContinue)) return parse_continue_stmt();
    if (match(TokenKind::KwReturn)) {
        auto* stmt = arena_.allocate<ASTStmt>();
        stmt->kind = StmtKind::Return;
        if (current_.kind != TokenKind::Semicolon) stmt->value_expr = parse_expression();
        expect(TokenKind::Semicolon, "expected ';' after return");
        return stmt;
    }

    // Variable declaration: Primitive type, or Custom type (Identifier followed by Identifier, Star, or LBracket)
    if ((current_.kind >= TokenKind::KwInt8 && current_.kind <= TokenKind::KwString32) ||
        (current_.kind == TokenKind::Identifier && (peek_token_.kind == TokenKind::Identifier || peek_token_.kind == TokenKind::Star || peek_token_.kind == TokenKind::LBracket))) {
        ASTType* ty = parse_type();
        return parse_var_decl_stmt(ty);
    }

    // Otherwise, parse as expression / generalized assignment
    ASTExpr* lval = parse_expression();

    if (match(TokenKind::Eq)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Assignment;
        s->target_expr = lval;
        s->value_expr = parse_expression();
        expect(TokenKind::Semicolon, "expected ';' after assignment");
        return s;
    }
    if (current_.kind == TokenKind::PlusEq || current_.kind == TokenKind::MinusEq ||
        current_.kind == TokenKind::StarEq || current_.kind == TokenKind::SlashEq ||
        current_.kind == TokenKind::PercentEq) {
        Token op_tok = current_;
        advance();
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::CompoundAssignment;
        s->target_expr = lval;
        s->op = op_tok.text;
        s->value_expr = parse_expression();
        expect(TokenKind::Semicolon, "expected ';' after compound assignment");
        return s;
    }
    if (match(TokenKind::PlusPlus)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Increment;
        s->target_expr = lval;
        expect(TokenKind::Semicolon, "expected ';' after ++");
        return s;
    }
    if (match(TokenKind::MinusMinus)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Decrement;
        s->target_expr = lval;
        expect(TokenKind::Semicolon, "expected ';' after --");
        return s;
    }

    expect(TokenKind::Semicolon, "expected ';' after statement");
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::ExprStmt;
    s->value_expr = lval;
    return s;
}

ASTStmt* Parser::parse_var_decl_stmt(ASTType* type_annot) {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::VarDecl;
    s->type_annot = type_annot;
    Token name_tok = expect(TokenKind::Identifier, "expected variable name");
    s->name = name_tok.text;
    expect(TokenKind::Eq, "variables must be explicitly initialized ('=')");
    s->init_expr = parse_expression();
    expect(TokenKind::Semicolon, "expected ';' after declaration");
    return s;
}

ASTStmt* Parser::parse_if_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::If;
    expect(TokenKind::LParen, "expected '(' after if");
    s->condition = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::KwThen, "expected 'then' before branch block");
    s->then_block = parse_block();
    if (match(TokenKind::KwElse)) {
        if (current_.kind == TokenKind::KwIf) {
            advance();
            s->else_block.push_back(parse_if_stmt());
        } else {
            s->else_block = parse_block();
        }
    }
    return s;
}

ASTStmt* Parser::parse_while_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::While;
    expect(TokenKind::LParen, "expected '(' after while");
    s->condition = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    s->then_block = parse_block();
    return s;
}

ASTStmt* Parser::parse_do_while_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::DoWhile;
    s->then_block = parse_block();
    expect(TokenKind::KwWhile, "expected 'while' after do block");
    expect(TokenKind::LParen, "expected '(' after while");
    s->condition = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::Semicolon, "expected ';' after do-while condition");
    return s;
}

ASTStmt* Parser::parse_switch_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::Switch;
    expect(TokenKind::LParen, "expected '(' after switch");
    s->condition = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    expect(TokenKind::LBrace, "expected '{' for switch body");

    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        ASTSwitchCase sc;
        sc.span = current_.span;
        if (match(TokenKind::KwCase)) {
            sc.match_val = parse_expression();
            sc.body = parse_block();
            s->switch_cases.push_back(sc);
        } else if (match(TokenKind::KwDefault)) {
            sc.match_val = nullptr;
            sc.body = parse_block();
            s->switch_cases.push_back(sc);
        } else {
            diag_.report_error(current_.span, "expected 'case' or 'default' in switch");
            advance();
        }
    }
    expect(TokenKind::RBrace, "expected '}' closing switch");
    return s;
}

ASTStmt* Parser::parse_break_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::Break;
    s->loop_levels = 1;
    if (match(TokenKind::LParen)) {
        Token lvl_tok = expect(TokenKind::IntLiteral, "expected positive integer for break levels");
        s->loop_levels = std::stoul(std::string(lvl_tok.text));
        expect(TokenKind::RParen, "expected ')'");
    }
    expect(TokenKind::Semicolon, "expected ';' after break");
    return s;
}

ASTStmt* Parser::parse_continue_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::Continue;
    s->loop_levels = 1;
    if (match(TokenKind::LParen)) {
        Token lvl_tok = expect(TokenKind::IntLiteral, "expected positive integer for continue levels");
        s->loop_levels = std::stoul(std::string(lvl_tok.text));
        expect(TokenKind::RParen, "expected ')'");
    }
    expect(TokenKind::Semicolon, "expected ';' after continue");
    return s;
}

ASTStmt* Parser::parse_foreach_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::Foreach;
    expect(TokenKind::LParen, "expected '(' after foreach");
    
    ASTType* t1 = parse_type();
    Token id1 = expect(TokenKind::Identifier, "expected loop variable");
    if (match(TokenKind::Comma)) {
        s->iter_idx = id1.text;
        s->iter_type = parse_type();
        Token id2 = expect(TokenKind::Identifier, "expected loop element variable");
        s->iter_var = id2.text;
    } else {
        s->iter_type = t1;
        s->iter_var = id1.text;
    }
    expect(TokenKind::KwIn, "expected 'in' keyword in foreach");
    s->iter_collection = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    s->then_block = parse_block();
    return s;
}

} // namespace femto