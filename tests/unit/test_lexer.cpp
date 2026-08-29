#include "test_framework.hpp"
#include "frontend/lexer.hpp"
#include "common/diagnostic.hpp"

using namespace femto;

TEST_CASE(Lexer, IntegerLiterals) {
    std::string src = "42 0xFF 0b1010 0o755 1_000_000";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    Token t1 = lexer.next_token();
    ASSERT_EQ(t1.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(t1.text, "42");

    Token t2 = lexer.next_token();
    ASSERT_EQ(t2.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(t2.text, "0xFF");

    Token t3 = lexer.next_token();
    ASSERT_EQ(t3.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(t3.text, "0b1010");

    Token t4 = lexer.next_token();
    ASSERT_EQ(t4.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(t4.text, "0o755");

    Token t5 = lexer.next_token();
    ASSERT_EQ(t5.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(t5.text, "1_000_000");

    ASSERT_EQ(lexer.next_token().kind, TokenKind::Eof);
    ASSERT_FALSE(diag.has_errors());
}

TEST_CASE(Lexer, FloatingPointLiterals) {
    std::string src = "3.1415 0.5 1.0e-9 2.5E+3";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    Token t1 = lexer.next_token();
    ASSERT_EQ(t1.kind, TokenKind::FloatLiteral);
    ASSERT_STREQ(t1.text, "3.1415");

    Token t2 = lexer.next_token();
    ASSERT_EQ(t2.kind, TokenKind::FloatLiteral);
    ASSERT_STREQ(t2.text, "0.5");

    Token t3 = lexer.next_token();
    ASSERT_EQ(t3.kind, TokenKind::FloatLiteral);
    ASSERT_STREQ(t3.text, "1.0e-9");

    Token t4 = lexer.next_token();
    ASSERT_EQ(t4.kind, TokenKind::FloatLiteral);
    ASSERT_STREQ(t4.text, "2.5E+3");
}

TEST_CASE(Lexer, StringAndCharLiterals) {
    std::string src = "\"hello\\nworld\" `raw\\path` 'A' '\\u{1F600}'";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    Token t1 = lexer.next_token();
    ASSERT_EQ(t1.kind, TokenKind::StringLiteral);
    ASSERT_STREQ(t1.text, "\"hello\\nworld\"");

    Token t2 = lexer.next_token();
    ASSERT_EQ(t2.kind, TokenKind::RawStringLiteral);
    ASSERT_STREQ(t2.text, "`raw\\path`");

    Token t3 = lexer.next_token();
    ASSERT_EQ(t3.kind, TokenKind::CharLiteral);
    ASSERT_STREQ(t3.text, "'A'");

    Token t4 = lexer.next_token();
    ASSERT_EQ(t4.kind, TokenKind::CharLiteral);
    ASSERT_STREQ(t4.text, "'\\u{1F600}'");
}

TEST_CASE(Lexer, MultiCharOperators) {
    std::string src = ":: -> .. ... ?? += -= *= /= %= ++ -- == != <= >= << >> && ||";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    ASSERT_EQ(lexer.next_token().kind, TokenKind::DoubleColon);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::Arrow);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::DotDot);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::DotDotDot);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::QuestionQuestion);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::PlusEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::MinusEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::StarEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::SlashEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::PercentEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::PlusPlus);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::MinusMinus);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::EqEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::BangEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::LtEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::GtEq);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::Shl);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::Shr);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::AmpAmp);
    ASSERT_EQ(lexer.next_token().kind, TokenKind::PipePipe);
}

TEST_CASE(Lexer, CommentsAndNesting) {
    std::string src = "// single line\n/* multi /* nested */ comment */ 123";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    Token tok = lexer.next_token();
    ASSERT_EQ(tok.kind, TokenKind::IntLiteral);
    ASSERT_STREQ(tok.text, "123");
}

TEST_CASE(Lexer, LineAndColumnTracking) {
    std::string src = "line1\n  line2_token";
    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Lexer lexer(sm, diag);

    Token t1 = lexer.next_token();
    auto lc1 = sm.get_line_col(t1.span.start);
    ASSERT_EQ(lc1.line, 1u);
    ASSERT_EQ(lc1.col, 1u);

    Token t2 = lexer.next_token();
    auto lc2 = sm.get_line_col(t2.span.start);
    ASSERT_EQ(lc2.line, 2u);
    ASSERT_EQ(lc2.col, 3u);
}