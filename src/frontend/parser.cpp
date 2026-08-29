#include "frontend/parser.hpp"
#include <cstring>
#include <cctype>

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

bool Parser::match_gt() {
    if (current_.kind == TokenKind::Gt) {
        advance();
        return true;
    }
    if (current_.kind == TokenKind::Shr) {
        current_.kind = TokenKind::Gt;
        current_.span.start.offset += 1;
        current_.span.length = 1;
        current_.text = current_.text.substr(1);
        return true;
    }
    return false;
}

bool Parser::is_generic_type_start() {
    if (current_.kind != TokenKind::Identifier || peek_token_.kind != TokenKind::Lt) {
        return false;
    }

    SourceManager dummy_sm(std::string(lexer_.source_manager().filename()), std::string(lexer_.source()));
    Diagnostics dummy_diag(dummy_sm);
    Lexer lookahead(lexer_.source_manager(), dummy_diag);
    lookahead.set_cursor(peek_token_.span.start.offset);

    Token tok = lookahead.next_token();
    if (tok.kind != TokenKind::Lt) return false;

    int depth = 1;
    while (true) {
        tok = lookahead.next_token();
        if (tok.kind == TokenKind::Eof || tok.kind == TokenKind::Semicolon || 
            tok.kind == TokenKind::LBrace || tok.kind == TokenKind::RBrace) {
            return false;
        }
        if (tok.kind == TokenKind::Lt) {
            depth++;
        } else if (tok.kind == TokenKind::Shl) {
            depth += 2;
        } else if (tok.kind == TokenKind::Gt) {
            depth--;
            if (depth == 0) {
                Token next = lookahead.next_token();
                if (next.kind == TokenKind::LParen) {
                    return false;
                }
                if (next.kind == TokenKind::Identifier || next.kind == TokenKind::Star || 
                    next.kind == TokenKind::LBracket || next.kind == TokenKind::Amp) {
                    return true;
                }
                return false;
            }
        } else if (tok.kind == TokenKind::Shr) {
            depth -= 2;
            if (depth <= 0) {
                Token next = lookahead.next_token();
                if (next.kind == TokenKind::LParen) {
                    return false;
                }
                if (next.kind == TokenKind::Identifier || next.kind == TokenKind::Star || 
                    next.kind == TokenKind::LBracket || next.kind == TokenKind::Amp) {
                    return true;
                }
                return false;
            }
        }
    }
    return false;
}

bool Parser::is_generic_call_at(uint32_t lt_offset) {
    SourceManager dummy_sm(std::string(lexer_.source_manager().filename()), std::string(lexer_.source()));
    Diagnostics dummy_diag(dummy_sm);
    Lexer lookahead(lexer_.source_manager(), dummy_diag);
    lookahead.set_cursor(lt_offset);

    Token tok = lookahead.next_token();
    if (tok.kind != TokenKind::Lt) return false;

    int depth = 1;
    while (true) {
        tok = lookahead.next_token();
        if (tok.kind == TokenKind::Eof || tok.kind == TokenKind::Semicolon || 
            tok.kind == TokenKind::LBrace || tok.kind == TokenKind::RBrace) {
            return false;
        }
        if (tok.kind == TokenKind::Lt) {
            depth++;
        } else if (tok.kind == TokenKind::Shl) {
            depth += 2;
        } else if (tok.kind == TokenKind::Gt) {
            depth--;
            if (depth == 0) {
                Token next = lookahead.next_token();
                return (next.kind == TokenKind::LParen);
            }
        } else if (tok.kind == TokenKind::Shr) {
            depth -= 2;
            if (depth <= 0) {
                Token next = lookahead.next_token();
                return (next.kind == TokenKind::LParen);
            }
        }
    }
    return false;
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
    } else if (current_.kind == TokenKind::KwVoid) {
        Token t = current_;
        advance();
        base_ty = arena_.allocate<ASTType>();
        base_ty->kind = TypeKind::Primitive;
        base_ty->primitive_kind = TokenKind::KwVoid;
        base_ty->span = t.span;
    } else if (current_.kind == TokenKind::Identifier) {
        Token id = current_;
        advance();
        base_ty = arena_.allocate<ASTType>();
        base_ty->kind = TypeKind::Custom;
        base_ty->custom_name = id.text;
        base_ty->span = id.span;

        if (match(TokenKind::Lt)) {
            if (current_.kind != TokenKind::Gt && current_.kind != TokenKind::Shr) {
                do {
                    base_ty->generic_args.push_back(parse_type());
                } while (match(TokenKind::Comma));
            }
            if (!match_gt()) {
                diag_.report_error(current_.span, "expected '>' closing generic type arguments");
            }
        }
    } else {
        diag_.report_error(current_.span, "expected type specifier");
        advance();
        return nullptr;
    }

    while (match(TokenKind::Star)) {
        ASTType* ptr_ty = arena_.allocate<ASTType>();
        ptr_ty->kind = TypeKind::Pointer;
        ptr_ty->pointee_or_element = base_ty;
        ptr_ty->span = base_ty->span.merge(current_.span);
        base_ty = ptr_ty;
    }

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

    // Builtins: @sizeof(T), @alignof(T), @bitcast(T, expr), @cast(T, expr)
    if (current_.kind == TokenKind::AtBuiltin) {
        Token b_tok = current_;
        advance();
        if (b_tok.text == "@sizeof") {
            expect(TokenKind::LParen, "expected '(' after @sizeof");
            ASTType* t = parse_type();
            expect(TokenKind::RParen, "expected ')' after type");
            ASTExpr* e = arena_.allocate<ASTExpr>();
            e->kind = ExprKind::BuiltinSizeof;
            e->target_type = t;
            e->span = span.merge(current_.span);
            return e;
        }
        if (b_tok.text == "@alignof") {
            expect(TokenKind::LParen, "expected '(' after @alignof");
            ASTType* t = parse_type();
            expect(TokenKind::RParen, "expected ')' after type");
            ASTExpr* e = arena_.allocate<ASTExpr>();
            e->kind = ExprKind::BuiltinAlignof;
            e->target_type = t;
            e->span = span.merge(current_.span);
            return e;
        }
        if (b_tok.text == "@cast") {
            expect(TokenKind::LParen, "expected '(' after @cast");
            ASTType* t = parse_type();
            expect(TokenKind::Comma, "expected ',' in @cast");
            ASTExpr* arg = parse_expression();
            expect(TokenKind::RParen, "expected ')'");
            ASTExpr* e = arena_.allocate<ASTExpr>();
            e->kind = ExprKind::Cast;
            e->target_type = t;
            e->left = arg;
            e->span = span.merge(current_.span);
            return e;
        }
        if (b_tok.text == "@bitcast") {
            expect(TokenKind::LParen, "expected '(' after @bitcast");
            ASTType* t = parse_type();
            expect(TokenKind::Comma, "expected ',' in @bitcast");
            ASTExpr* arg = parse_expression();
            expect(TokenKind::RParen, "expected ')'");
            ASTExpr* e = arena_.allocate<ASTExpr>();
            e->kind = ExprKind::BuiltinBitcast;
            e->target_type = t;
            e->left = arg;
            e->span = span.merge(current_.span);
            return e;
        }
    }

    if (match(TokenKind::KwSuccess)) {
        expect(TokenKind::LParen, "expected '(' after success");
        ASTExpr* val = nullptr;
        if (current_.kind != TokenKind::RParen) {
            val = parse_expression();
        }
        expect(TokenKind::RParen, "expected ')'");
        ASTExpr* sc = arena_.allocate<ASTExpr>();
        sc->kind = ExprKind::Unary;
        sc->op = "success";
        sc->left = val;
        sc->span = span.merge(current_.span);
        return sc;
    }

    if (match(TokenKind::KwFailure)) {
        expect(TokenKind::LParen, "expected '(' after failure");
        ASTExpr* code = nullptr;
        if (current_.kind != TokenKind::RParen) {
            code = parse_expression();
        }
        expect(TokenKind::RParen, "expected ')'");
        ASTExpr* fl = arena_.allocate<ASTExpr>();
        fl->kind = ExprKind::Unary;
        fl->op = "failure";
        fl->left = code;
        fl->span = span.merge(current_.span);
        return fl;
    }

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
        current_.kind == TokenKind::CharLiteral || current_.kind == TokenKind::KwTrue || 
        current_.kind == TokenKind::KwFalse || current_.kind == TokenKind::KwNull) {
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

        std::string q_name = std::string(id.text);
        while (match(TokenKind::DoubleColon)) {
            Token member = expect(TokenKind::Identifier, "expected identifier after '::'");
            q_name += "::" + std::string(member.text);
        }

        char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
        std::memcpy(q_buf, q_name.data(), q_name.size());
        q_buf[q_name.size()] = '\0';
        std::string_view full_id(q_buf, q_name.size());

        if (current_.kind == TokenKind::Lt && is_generic_call_at(current_.span.start.offset)) {
            advance();
            std::vector<ASTType*> g_args;
            if (current_.kind != TokenKind::Gt && current_.kind != TokenKind::Shr) {
                do {
                    g_args.push_back(parse_type());
                } while (match(TokenKind::Comma));
            }
            if (!match_gt()) {
                diag_.report_error(current_.span, "expected '>' closing generic call arguments");
            }
            expect(TokenKind::LParen, "expected '(' for call arguments");

            ASTExpr* call = arena_.allocate<ASTExpr>();
            call->kind = ExprKind::Call;
            ASTExpr* callee = arena_.allocate<ASTExpr>();
            callee->kind = ExprKind::Identifier;
            callee->raw_text = full_id;
            call->left = callee;
            call->generic_args = std::move(g_args);

            if (current_.kind != TokenKind::RParen) {
                do {
                    call->args.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')'");
            return call;
        }

        ASTExpr* e = arena_.allocate<ASTExpr>();
        e->kind = ExprKind::Identifier;
        e->raw_text = full_id;
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
        if (current_.kind == TokenKind::QuestionQuestion) {
            if (peek_token_.kind == TokenKind::LParen) {
                break;
            }
            advance();
            ASTExpr* unwrap = arena_.allocate<ASTExpr>();
            unwrap->kind = ExprKind::PostfixUnwrap;
            unwrap->left = left;
            unwrap->span = left->span.merge(current_.span);
            left = unwrap;
            continue;
        }

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
    if (match(TokenKind::KwImport)) {
        Token base = expect(TokenKind::Identifier, "expected module name after import");
        std::string mod_path = std::string(base.text);
        while (match(TokenKind::DoubleColon)) {
            Token sub = expect(TokenKind::Identifier, "expected submodule name after '::'");
            mod_path += "/" + std::string(sub.text);
        }
        if (current_.kind == TokenKind::Identifier && current_.text == "as") {
            advance();
            expect(TokenKind::Identifier, "expected alias name after 'as'");
        }
        expect(TokenKind::Semicolon, "expected ';' after import");
        prog.imports.push_back(mod_path);
        return;
    }

    if (match(TokenKind::KwNamespace)) {
        Token base = expect(TokenKind::Identifier, "expected namespace name");
        std::string ns_name = std::string(base.text);
        while (match(TokenKind::DoubleColon)) {
            Token sub = expect(TokenKind::Identifier, "expected sub-namespace name after '::'");
            ns_name += "::" + std::string(sub.text);
        }
        expect(TokenKind::LBrace, "expected '{' for namespace body");

        size_t fn_start = prog.functions.size();
        size_t st_start = prog.structs.size();
        size_t un_start = prog.unions.size();
        size_t en_start = prog.enums.size();
        size_t cn_start = prog.constants.size();

        while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
            bool exp = match(TokenKind::HashExport);
            parse_top_level_declaration(prog, exp);
        }
        expect(TokenKind::RBrace, "expected '}' closing namespace");

        for (size_t i = fn_start; i < prog.functions.size(); ++i) {
            std::string q_name = ns_name + "::" + std::string(prog.functions[i]->name);
            char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
            std::memcpy(q_buf, q_name.data(), q_name.size());
            q_buf[q_name.size()] = '\0';
            prog.functions[i]->name = std::string_view(q_buf, q_name.size());
        }
        for (size_t i = st_start; i < prog.structs.size(); ++i) {
            std::string q_name = ns_name + "::" + std::string(prog.structs[i]->name);
            char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
            std::memcpy(q_buf, q_name.data(), q_name.size());
            q_buf[q_name.size()] = '\0';
            prog.structs[i]->name = std::string_view(q_buf, q_name.size());
        }
        for (size_t i = un_start; i < prog.unions.size(); ++i) {
            std::string q_name = ns_name + "::" + std::string(prog.unions[i]->name);
            char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
            std::memcpy(q_buf, q_name.data(), q_name.size());
            q_buf[q_name.size()] = '\0';
            prog.unions[i]->name = std::string_view(q_buf, q_name.size());
        }
        for (size_t i = en_start; i < prog.enums.size(); ++i) {
            std::string q_name = ns_name + "::" + std::string(prog.enums[i]->name);
            char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
            std::memcpy(q_buf, q_name.data(), q_name.size());
            q_buf[q_name.size()] = '\0';
            prog.enums[i]->name = std::string_view(q_buf, q_name.size());
        }
        for (size_t i = cn_start; i < prog.constants.size(); ++i) {
            std::string q_name = ns_name + "::" + std::string(prog.constants[i]->name);
            char* q_buf = (char*)arena_.allocate_bytes(q_name.size() + 1, 1);
            std::memcpy(q_buf, q_name.data(), q_name.size());
            q_buf[q_name.size()] = '\0';
            prog.constants[i]->name = std::string_view(q_buf, q_name.size());
        }
        return;
    }

    if (match(TokenKind::KwExtern)) {
        expect(TokenKind::StringLiteral, "expected \"C\" after extern");
        if (match(TokenKind::LBrace)) {
            while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
                Token name_tok = expect(TokenKind::Identifier, "expected function name in extern block");
                expect(TokenKind::DoubleColon, "expected '::' after identifier");
                prog.functions.push_back(parse_function_decl(name_tok.text, {}, false, /*is_extern_c=*/true));
                match(TokenKind::Semicolon);
            }
            expect(TokenKind::RBrace, "expected '}' closing extern block");
        } else {
            Token name_tok = expect(TokenKind::Identifier, "expected function name");
            expect(TokenKind::DoubleColon, "expected '::'");
            prog.functions.push_back(parse_function_decl(name_tok.text, {}, false, /*is_extern_c=*/true));
            match(TokenKind::Semicolon);
        }
        return;
    }

    if (current_.kind == TokenKind::Identifier) {
        Token name_tok = current_;
        advance();
        expect(TokenKind::DoubleColon, "expected '::' after identifier");

        if (match(TokenKind::KwStruct)) {
            prog.structs.push_back(parse_struct_decl(name_tok.text, is_exported));
        } else if (match(TokenKind::KwUnion)) {
            prog.unions.push_back(parse_union_decl(name_tok.text, is_exported));
        } else if (match(TokenKind::KwEnum)) {
            prog.enums.push_back(parse_enum_decl(name_tok.text, is_exported));
        } else if (match(TokenKind::Lt)) {
            std::vector<std::string_view> g_params;
            if (current_.kind != TokenKind::Gt && current_.kind != TokenKind::Shr) {
                do {
                    Token p = expect(TokenKind::Identifier, "expected generic parameter name");
                    g_params.push_back(p.text);
                } while (match(TokenKind::Comma));
            }
            if (!match_gt()) {
                diag_.report_error(current_.span, "expected '>' closing generic parameters");
            }
            prog.functions.push_back(parse_function_decl(name_tok.text, std::move(g_params), is_exported));
        } else if (current_.kind == TokenKind::LParen) {
            prog.functions.push_back(parse_function_decl(name_tok.text, {}, is_exported));
        } else {
            prog.constants.push_back(parse_const_decl(name_tok.text, is_exported));
        }
    } else if (match(TokenKind::HashIf)) {
        expect(TokenKind::LParen, "expected '(' after #if");
        ASTExpr* cond = parse_expression();
        expect(TokenKind::RParen, "expected ')'");
        expect(TokenKind::LBrace, "expected '{'");
        while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
            bool exp = match(TokenKind::HashExport);
            parse_top_level_declaration(prog, exp);
        }
        expect(TokenKind::RBrace, "expected '}'");
        if (match(TokenKind::HashElse)) {
            expect(TokenKind::LBrace, "expected '{'");
            while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
                bool exp = match(TokenKind::HashExport);
                parse_top_level_declaration(prog, exp);
            }
            expect(TokenKind::RBrace, "expected '}'");
        }
    } else {
        diag_.report_error(current_.span, "expected top-level declaration");
        advance();
    }
}

ASTConstDecl* Parser::parse_const_decl(std::string_view name, bool is_exported) {
    auto* c = arena_.allocate<ASTConstDecl>();
    c->name = name;
    c->is_exported = is_exported;
    c->init_expr = parse_expression();
    expect(TokenKind::Semicolon, "expected ';' after constant declaration");
    return c;
}

ASTFunctionDecl* Parser::parse_function_decl(std::string_view name, std::vector<std::string_view> generic_params, bool is_exported, bool is_extern_c) {
    auto* fn = arena_.allocate<ASTFunctionDecl>();
    fn->name = name;
    fn->generic_params = std::move(generic_params);
    fn->is_exported = is_exported;
    fn->is_extern_c = is_extern_c;
    fn->span = current_.span;

    expect(TokenKind::LParen, "expected '(' for parameter list");
    if (current_.kind != TokenKind::RParen) {
        do {
            if (match(TokenKind::DotDotDot)) {
                fn->is_variadic = true;
                break;
            }
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
    
    if (is_extern_c) {
        return fn;
    }
    
    fn->body = parse_block();
    return fn;
}

ASTStructDecl* Parser::parse_struct_decl(std::string_view name, bool is_exported) {
    auto* s = arena_.allocate<ASTStructDecl>();
    s->name = name;
    s->is_exported = is_exported;

    if (match(TokenKind::Lt)) {
        if (current_.kind != TokenKind::Gt && current_.kind != TokenKind::Shr) {
            do {
                Token p = expect(TokenKind::Identifier, "expected generic parameter name");
                s->generic_params.push_back(p.text);
            } while (match(TokenKind::Comma));
        }
        if (!match_gt()) {
            diag_.report_error(current_.span, "expected '>' closing generic struct parameters");
        }
    }

    expect(TokenKind::LBrace, "expected '{' for struct body");
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        ASTStructField f;
        f.type = parse_type();
        Token f_name = expect(TokenKind::Identifier, "expected field name");
        f.name = f_name.text;
        if (match(TokenKind::Eq)) {
            f.default_value = parse_expression();
        }
        match(TokenKind::Semicolon);
        s->fields.push_back(f);
    }
    expect(TokenKind::RBrace, "expected '}' closing struct");
    return s;
}

ASTUnionDecl* Parser::parse_union_decl(std::string_view name, bool is_exported) {
    auto* u = arena_.allocate<ASTUnionDecl>();
    u->name = name;
    u->is_exported = is_exported;

    if (match(TokenKind::Lt)) {
        if (current_.kind != TokenKind::Gt && current_.kind != TokenKind::Shr) {
            do {
                Token p = expect(TokenKind::Identifier, "expected generic parameter name");
                u->generic_params.push_back(p.text);
            } while (match(TokenKind::Comma));
        }
        if (!match_gt()) {
            diag_.report_error(current_.span, "expected '>' closing generic union parameters");
        }
    }

    expect(TokenKind::LBrace, "expected '{' for union body");
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        ASTUnionField f;
        f.type = parse_type();
        Token f_name = expect(TokenKind::Identifier, "expected field name");
        f.name = f_name.text;
        if (match(TokenKind::Eq)) {
            f.default_value = parse_expression();
        }
        match(TokenKind::Semicolon);
        u->fields.push_back(f);
    }
    expect(TokenKind::RBrace, "expected '}' closing union");
    return u;
}

ASTEnumDecl* Parser::parse_enum_decl(std::string_view name, bool is_exported) {
    auto* e = arena_.allocate<ASTEnumDecl>();
    e->name = name;
    e->is_exported = is_exported;
    expect(TokenKind::Arrow, "expected '->' specifying enum backing type");
    e->backing_type = parse_type();
    expect(TokenKind::LBrace, "expected '{' for enum body");
    int64_t cur_val = 0;
    while (current_.kind != TokenKind::RBrace && current_.kind != TokenKind::Eof) {
        Token var_name = expect(TokenKind::Identifier, "expected enum variant name");
        ASTEnumVariant v;
        v.name = var_name.text;
        if (match(TokenKind::Eq)) {
            Token val_tok = expect(TokenKind::IntLiteral, "expected integer for enum variant");
            cur_val = std::stoll(std::string(val_tok.text));
            v.value = cur_val;
        } else {
            v.value = cur_val;
        }
        cur_val++;
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
    SourceSpan start_span = current_.span;

    if (match(TokenKind::KwConst)) {
        ASTType* ty = parse_type();
        auto* s = parse_var_decl_stmt(ty);
        s->is_const = true;
        s->span = start_span.merge(current_.span);
        return s;
    }

    if (match(TokenKind::KwIf)) return parse_if_stmt();
    if (match(TokenKind::HashIf)) return parse_hash_if_stmt();
    if (match(TokenKind::KwWhile)) return parse_while_stmt();
    if (match(TokenKind::KwDo)) return parse_do_while_stmt();
    if (match(TokenKind::KwFor)) return parse_for_stmt();
    if (match(TokenKind::KwSwitch)) return parse_switch_stmt();
    if (match(TokenKind::KwForeach)) return parse_foreach_stmt();
    if (match(TokenKind::KwBreak)) return parse_break_stmt();
    if (match(TokenKind::KwContinue)) return parse_continue_stmt();
    if (match(TokenKind::KwReturn)) {
        auto* stmt = arena_.allocate<ASTStmt>();
        stmt->kind = StmtKind::Return;
        if (current_.kind != TokenKind::Semicolon) stmt->value_expr = parse_expression();
        expect(TokenKind::Semicolon, "expected ';' after return");
        stmt->span = start_span.merge(current_.span);
        return stmt;
    }

    if (current_.kind >= TokenKind::KwInt8 && current_.kind <= TokenKind::KwString32) {
        ASTType* ty = parse_type();
        return parse_var_decl_stmt(ty);
    }

    if (current_.kind == TokenKind::Bang) {
        if ((peek_token_.kind >= TokenKind::KwInt8 && peek_token_.kind <= TokenKind::KwString32) ||
            peek_token_.kind == TokenKind::KwVoid) {
            ASTType* ty = parse_type();
            return parse_var_decl_stmt(ty);
        }
        if (peek_token_.kind == TokenKind::Identifier) {
            SourceManager dummy_sm(std::string(lexer_.source_manager().filename()), std::string(lexer_.source()));
            Diagnostics dummy_diag(dummy_sm);
            Lexer lookahead(lexer_.source_manager(), dummy_diag);
            lookahead.set_cursor(current_.span.start.offset);
            lookahead.next_token(); // '!'
            Token t1 = lookahead.next_token(); // CustomType
            Token t2 = lookahead.next_token(); // following token
            if (t2.kind == TokenKind::Identifier || t2.kind == TokenKind::Star || t2.kind == TokenKind::LBracket || t2.kind == TokenKind::Lt) {
                ASTType* ty = parse_type();
                return parse_var_decl_stmt(ty);
            }
        }
    }

    if (current_.kind == TokenKind::Identifier) {
        if (peek_token_.kind == TokenKind::Identifier || peek_token_.kind == TokenKind::Star || is_generic_type_start()) {
            ASTType* ty = parse_type();
            return parse_var_decl_stmt(ty);
        }
    }

    ASTExpr* lval = parse_expression();

    if (match(TokenKind::QuestionQuestion)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::ResultBranch;
        s->condition = lval;

        expect(TokenKind::LParen, "expected '(' for success variable");
        parse_type();
        Token s_var = expect(TokenKind::Identifier, "expected success variable name");
        s->success_var = s_var.text;
        expect(TokenKind::RParen, "expected ')'");
        s->success_block = parse_block();

        expect(TokenKind::Colon, "expected ':' separating result branch");
        expect(TokenKind::LParen, "expected '(' for failure variable");
        parse_type();
        Token f_var = expect(TokenKind::Identifier, "expected failure variable name");
        s->failure_var = f_var.text;
        expect(TokenKind::RParen, "expected ')'");
        s->failure_block = parse_block();

        match(TokenKind::Semicolon);
        s->span = start_span.merge(current_.span);
        return s;
    }

    if (match(TokenKind::Eq)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Assignment;
        s->target_expr = lval;
        s->value_expr = parse_expression();
        expect(TokenKind::Semicolon, "expected ';' after assignment");
        s->span = lval ? lval->span.merge(current_.span) : start_span.merge(current_.span);
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
        s->span = lval ? lval->span.merge(current_.span) : start_span.merge(current_.span);
        return s;
    }
    if (match(TokenKind::PlusPlus)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Increment;
        s->target_expr = lval;
        expect(TokenKind::Semicolon, "expected ';' after ++");
        s->span = lval ? lval->span.merge(current_.span) : start_span.merge(current_.span);
        return s;
    }
    if (match(TokenKind::MinusMinus)) {
        auto* s = arena_.allocate<ASTStmt>();
        s->kind = StmtKind::Decrement;
        s->target_expr = lval;
        expect(TokenKind::Semicolon, "expected ';' after --");
        s->span = lval ? lval->span.merge(current_.span) : start_span.merge(current_.span);
        return s;
    }

    expect(TokenKind::Semicolon, "expected ';' after statement");
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::ExprStmt;
    s->value_expr = lval;
    s->span = lval ? lval->span.merge(current_.span) : start_span.merge(current_.span);
    return s;
}

ASTStmt* Parser::parse_hash_if_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::HashIf;
    expect(TokenKind::LParen, "expected '(' after #if");
    s->condition = parse_expression();
    expect(TokenKind::RParen, "expected ')'");
    s->then_block = parse_block();
    if (match(TokenKind::HashElse)) {
        s->else_block = parse_block();
    }
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
    s->span = type_annot ? type_annot->span.merge(current_.span) : name_tok.span.merge(current_.span);
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

ASTStmt* Parser::parse_for_stmt() {
    auto* s = arena_.allocate<ASTStmt>();
    s->kind = StmtKind::For;
    expect(TokenKind::LParen, "expected '(' after for");

    if (current_.kind != TokenKind::Semicolon) {
        if (match(TokenKind::KwConst)) {
            ASTType* ty = parse_type();
            s->init_stmt = parse_var_decl_stmt(ty);
            s->init_stmt->is_const = true;
        } else if (current_.kind >= TokenKind::KwInt8 && current_.kind <= TokenKind::KwString32) {
            ASTType* ty = parse_type();
            s->init_stmt = parse_var_decl_stmt(ty);
        } else if (current_.kind == TokenKind::Identifier && 
                  (peek_token_.kind == TokenKind::Identifier || peek_token_.kind == TokenKind::Star || is_generic_type_start())) {
            ASTType* ty = parse_type();
            s->init_stmt = parse_var_decl_stmt(ty);
        } else {
            ASTExpr* lval = parse_expression();
            if (match(TokenKind::Eq)) {
                auto* asgn = arena_.allocate<ASTStmt>();
                asgn->kind = StmtKind::Assignment;
                asgn->target_expr = lval;
                asgn->value_expr = parse_expression();
                expect(TokenKind::Semicolon, "expected ';' after for-init assignment");
                s->init_stmt = asgn;
            } else if (match(TokenKind::PlusPlus)) {
                auto* inc = arena_.allocate<ASTStmt>();
                inc->kind = StmtKind::Increment;
                inc->target_expr = lval;
                expect(TokenKind::Semicolon, "expected ';' after ++");
                s->init_stmt = inc;
            } else if (match(TokenKind::MinusMinus)) {
                auto* dec = arena_.allocate<ASTStmt>();
                dec->kind = StmtKind::Decrement;
                dec->target_expr = lval;
                expect(TokenKind::Semicolon, "expected ';' after --");
                s->init_stmt = dec;
            } else {
                expect(TokenKind::Semicolon, "expected ';' after for-init");
                auto* es = arena_.allocate<ASTStmt>();
                es->kind = StmtKind::ExprStmt;
                es->value_expr = lval;
                s->init_stmt = es;
            }
        }
    } else {
        advance();
    }

    if (current_.kind != TokenKind::Semicolon) {
        s->condition = parse_expression();
    }
    expect(TokenKind::Semicolon, "expected ';' after for condition");

    if (current_.kind != TokenKind::RParen) {
        ASTExpr* lval = parse_expression();
        if (match(TokenKind::PlusPlus)) {
            auto* step = arena_.allocate<ASTStmt>();
            step->kind = StmtKind::Increment;
            step->target_expr = lval;
            s->step_stmt = step;
        } else if (match(TokenKind::MinusMinus)) {
            auto* step = arena_.allocate<ASTStmt>();
            step->kind = StmtKind::Decrement;
            step->target_expr = lval;
            s->step_stmt = step;
        } else if (match(TokenKind::Eq)) {
            auto* step = arena_.allocate<ASTStmt>();
            step->kind = StmtKind::Assignment;
            step->target_expr = lval;
            step->value_expr = parse_expression();
            s->step_stmt = step;
        } else if (current_.kind == TokenKind::PlusEq || current_.kind == TokenKind::MinusEq ||
                   current_.kind == TokenKind::StarEq || current_.kind == TokenKind::SlashEq ||
                   current_.kind == TokenKind::PercentEq) {
            Token op_tok = current_;
            advance();
            auto* step = arena_.allocate<ASTStmt>();
            step->kind = StmtKind::CompoundAssignment;
            step->target_expr = lval;
            step->op = op_tok.text;
            step->value_expr = parse_expression();
            s->step_stmt = step;
        } else {
            auto* step = arena_.allocate<ASTStmt>();
            step->kind = StmtKind::ExprStmt;
            step->value_expr = lval;
            s->step_stmt = step;
        }
    }
    expect(TokenKind::RParen, "expected ')' after for clauses");
    s->then_block = parse_block();
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