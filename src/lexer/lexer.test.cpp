#include <gtest/gtest.h>
#include "lexer/lexer.h"
#include "common/diagnostic.h"

using namespace femto;

class LexerTest : public ::testing::Test {
protected:
    std::vector<Token> lex(const std::string& source) {
        source_ = source;
        diag_ = std::make_unique<DiagnosticEngine>("test.femto", source_);
        Lexer lexer(source_, *diag_);
        return lexer.tokenize();
    }

    bool has_errors() const { return diag_->has_errors(); }

private:
    std::string source_;
    std::unique_ptr<DiagnosticEngine> diag_;
};

// Integer literals
TEST_F(LexerTest, IntegerLiteralDecimal) {
    auto tokens = lex("42");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 42);
}

TEST_F(LexerTest, IntegerLiteralHex) {
    auto tokens = lex("0xFF");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 0xFF);
}

TEST_F(LexerTest, IntegerLiteralBinary) {
    auto tokens = lex("0b1010");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 10);
}

TEST_F(LexerTest, IntegerLiteralOctal) {
    auto tokens = lex("0o755");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 0755);
}

TEST_F(LexerTest, IntegerLiteralWithSeparator) {
    auto tokens = lex("1_000_000");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 1000000);
}

// Float literals
TEST_F(LexerTest, FloatLiteral) {
    auto tokens = lex("3.14");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::FloatLiteral);
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[0].value), 3.14);
}

TEST_F(LexerTest, FloatLiteralScientific) {
    auto tokens = lex("1.0e-9");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::FloatLiteral);
    EXPECT_DOUBLE_EQ(std::get<double>(tokens[0].value), 1.0e-9);
}

// Char literals
TEST_F(LexerTest, CharLiteral) {
    auto tokens = lex("'a'");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::CharLiteral);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "a");
}

TEST_F(LexerTest, CharLiteralEscape) {
    auto tokens = lex("'\\n'");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::CharLiteral);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "\n");
}

// String literals
TEST_F(LexerTest, StringLiteral) {
    auto tokens = lex("\"hello\"");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "hello");
}

TEST_F(LexerTest, RawStringLiteral) {
    auto tokens = lex("`C:\\path\\{not interpolated}`");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "C:\\path\\{not interpolated}");
}

TEST_F(LexerTest, StringLiteralEscape) {
    auto tokens = lex("\"hello\\nworld\"");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::StringLiteral);
    EXPECT_EQ(std::get<std::string>(tokens[0].value), "hello\nworld");
}

// Bool literals
TEST_F(LexerTest, BoolLiteralTrue) {
    auto tokens = lex("true");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::BoolLiteral);
}

TEST_F(LexerTest, BoolLiteralFalse) {
    auto tokens = lex("false");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::BoolLiteral);
}

// Keywords
TEST_F(LexerTest, Keywords) {
    auto tokens = lex("import for while do if then else switch case match return break continue enum struct null extern foreach array namespace success failure void in default const union");
    // Should produce keyword tokens + EOF
    EXPECT_GE(tokens.size(), 23u);
    EXPECT_EQ(tokens[0].type, TokenType::KW_import);
    EXPECT_EQ(tokens[1].type, TokenType::KW_for);
    EXPECT_EQ(tokens[2].type, TokenType::KW_while);
    EXPECT_EQ(tokens[3].type, TokenType::KW_do);
    EXPECT_EQ(tokens[4].type, TokenType::KW_if);
    EXPECT_EQ(tokens[5].type, TokenType::KW_then);
    EXPECT_EQ(tokens[6].type, TokenType::KW_else);
    EXPECT_EQ(tokens[7].type, TokenType::KW_switch);
    EXPECT_EQ(tokens[8].type, TokenType::KW_case);
    EXPECT_EQ(tokens[9].type, TokenType::KW_match);
    EXPECT_EQ(tokens[10].type, TokenType::KW_return);
    EXPECT_EQ(tokens[11].type, TokenType::KW_break);
    EXPECT_EQ(tokens[12].type, TokenType::KW_continue);
    EXPECT_EQ(tokens[13].type, TokenType::KW_enum);
    EXPECT_EQ(tokens[14].type, TokenType::KW_struct);
    EXPECT_EQ(tokens[15].type, TokenType::KW_null);
    EXPECT_EQ(tokens[16].type, TokenType::KW_extern);
    EXPECT_EQ(tokens[17].type, TokenType::KW_foreach);
    EXPECT_EQ(tokens[18].type, TokenType::KW_array);
    EXPECT_EQ(tokens[19].type, TokenType::KW_namespace);
    EXPECT_EQ(tokens[20].type, TokenType::KW_success);
    EXPECT_EQ(tokens[21].type, TokenType::KW_failure);
    EXPECT_EQ(tokens[22].type, TokenType::KW_void);
}

// Builtin types
TEST_F(LexerTest, BuiltinTypes) {
    auto tokens = lex("int32 uint64 float64 char8 string8 bool32");
    EXPECT_EQ(tokens[0].type, TokenType::TY_int32);
    EXPECT_EQ(tokens[1].type, TokenType::TY_uint64);
    EXPECT_EQ(tokens[2].type, TokenType::TY_float64);
    EXPECT_EQ(tokens[3].type, TokenType::TY_char8);
    EXPECT_EQ(tokens[4].type, TokenType::TY_string8);
    EXPECT_EQ(tokens[5].type, TokenType::TY_bool32);
}

// Operators
TEST_F(LexerTest, ArithmeticOperators) {
    auto tokens = lex("+ - * / %");
    EXPECT_EQ(tokens[0].type, TokenType::Plus);
    EXPECT_EQ(tokens[1].type, TokenType::Minus);
    EXPECT_EQ(tokens[2].type, TokenType::Star);
    EXPECT_EQ(tokens[3].type, TokenType::Slash);
    EXPECT_EQ(tokens[4].type, TokenType::Percent);
}

TEST_F(LexerTest, CompoundOperators) {
    auto tokens = lex("+= -= *= /= %= &= |= ^= <<= >>=");
    EXPECT_EQ(tokens[0].type, TokenType::PlusEq);
    EXPECT_EQ(tokens[1].type, TokenType::MinusEq);
    EXPECT_EQ(tokens[2].type, TokenType::StarEq);
    EXPECT_EQ(tokens[3].type, TokenType::SlashEq);
    EXPECT_EQ(tokens[4].type, TokenType::PercentEq);
    EXPECT_EQ(tokens[5].type, TokenType::AmpEq);
    EXPECT_EQ(tokens[6].type, TokenType::PipeEq);
    EXPECT_EQ(tokens[7].type, TokenType::CaretEq);
    EXPECT_EQ(tokens[8].type, TokenType::LShiftEq);
    EXPECT_EQ(tokens[9].type, TokenType::RShiftEq);
}

TEST_F(LexerTest, ComparisonOperators) {
    auto tokens = lex("== != < > <= >=");
    EXPECT_EQ(tokens[0].type, TokenType::EqEq);
    EXPECT_EQ(tokens[1].type, TokenType::Neq);
    EXPECT_EQ(tokens[2].type, TokenType::Lt);
    EXPECT_EQ(tokens[3].type, TokenType::Gt);
    EXPECT_EQ(tokens[4].type, TokenType::Le);
    EXPECT_EQ(tokens[5].type, TokenType::Ge);
}

TEST_F(LexerTest, SpecialOperators) {
    auto tokens = lex(":: -> ?? ++ -- @ #");
    EXPECT_EQ(tokens[0].type, TokenType::DoubleColon);
    EXPECT_EQ(tokens[1].type, TokenType::Arrow);
    EXPECT_EQ(tokens[2].type, TokenType::DoubleQuestion);
    EXPECT_EQ(tokens[3].type, TokenType::PlusPlus);
    EXPECT_EQ(tokens[4].type, TokenType::MinusMinus);
    EXPECT_EQ(tokens[5].type, TokenType::At);
    EXPECT_EQ(tokens[6].type, TokenType::Hash);
}

// Comments
TEST_F(LexerTest, LineComment) {
    auto tokens = lex("42 // this is a comment");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 42);
}

TEST_F(LexerTest, BlockComment) {
    auto tokens = lex("42 /* comment */ 7");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 42);
    EXPECT_EQ(tokens[1].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[1].value), 7);
}

TEST_F(LexerTest, NestedBlockComment) {
    auto tokens = lex("/* outer /* inner */ still comment */ 10");
    ASSERT_GE(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 10);
}

// Delimiters
TEST_F(LexerTest, Delimiters) {
    auto tokens = lex("( ) { } [ ] , ; . :");
    EXPECT_EQ(tokens[0].type, TokenType::LParen);
    EXPECT_EQ(tokens[1].type, TokenType::RParen);
    EXPECT_EQ(tokens[2].type, TokenType::LBrace);
    EXPECT_EQ(tokens[3].type, TokenType::RBrace);
    EXPECT_EQ(tokens[4].type, TokenType::LBracket);
    EXPECT_EQ(tokens[5].type, TokenType::RBracket);
    EXPECT_EQ(tokens[6].type, TokenType::Comma);
    EXPECT_EQ(tokens[7].type, TokenType::Semicolon);
    EXPECT_EQ(tokens[8].type, TokenType::Dot);
    EXPECT_EQ(tokens[9].type, TokenType::Colon);
}

// @-builtins
TEST_F(LexerTest, AtBuiltins) {
    auto tokens = lex("@target @arch @endian @file @line @sizeof @alignof @typeof @bitcast");
    EXPECT_EQ(tokens.size(), 10u); // 9 builtins + EOF
    for (int i = 0; i < 9; ++i) {
        EXPECT_EQ(tokens[i].type, TokenType::Identifier);
        EXPECT_TRUE(tokens[i].lexeme.starts_with("@"));
    }
}

// # directives
TEST_F(LexerTest, HashDirectives) {
    auto tokens = lex("#export #if #else");
    EXPECT_EQ(tokens[0].type, TokenType::HashExport);
    EXPECT_EQ(tokens[1].type, TokenType::HashIf);
    EXPECT_EQ(tokens[2].type, TokenType::HashElse);
}

// Function declaration
TEST_F(LexerTest, FunctionDeclaration) {
    auto tokens = lex("add :: (int32 a, int32 b) -> int32 { return a + b; }");
    EXPECT_EQ(tokens[0].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].type, TokenType::DoubleColon);
    EXPECT_EQ(tokens[2].type, TokenType::LParen);
    EXPECT_EQ(tokens[3].type, TokenType::TY_int32);
    EXPECT_EQ(tokens[4].type, TokenType::Identifier);
    EXPECT_EQ(tokens[5].type, TokenType::Comma);
    EXPECT_EQ(tokens[6].type, TokenType::TY_int32);
    EXPECT_EQ(tokens[7].type, TokenType::Identifier);
    EXPECT_EQ(tokens[8].type, TokenType::RParen);
    EXPECT_EQ(tokens[9].type, TokenType::Arrow);
    EXPECT_EQ(tokens[10].type, TokenType::TY_int32);
    EXPECT_EQ(tokens[11].type, TokenType::LBrace);
    EXPECT_EQ(tokens[12].type, TokenType::KW_return);
}

// Struct declaration
TEST_F(LexerTest, StructDeclaration) {
    auto tokens = lex("Point :: struct { float32 x = 0.0; float32 y = 0.0; }");
    EXPECT_EQ(tokens[0].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].type, TokenType::DoubleColon);
    EXPECT_EQ(tokens[2].type, TokenType::KW_struct);
    EXPECT_EQ(tokens[3].type, TokenType::LBrace);
    EXPECT_EQ(tokens[4].type, TokenType::TY_float32);
    EXPECT_EQ(tokens[5].type, TokenType::Identifier);
    EXPECT_EQ(tokens[6].type, TokenType::Eq);
}

// Enum declaration
TEST_F(LexerTest, EnumDeclaration) {
    auto tokens = lex("Color :: enum -> uint8 { red = 1, green, blue, }");
    EXPECT_EQ(tokens[0].type, TokenType::Identifier);
    EXPECT_EQ(tokens[1].type, TokenType::DoubleColon);
    EXPECT_EQ(tokens[2].type, TokenType::KW_enum);
    EXPECT_EQ(tokens[3].type, TokenType::Arrow);
    EXPECT_EQ(tokens[4].type, TokenType::TY_uint8);
}

// Error cases
TEST_F(LexerTest, UnterminatedString) {
    auto tokens = lex("\"hello");
    EXPECT_TRUE(has_errors());
}

TEST_F(LexerTest, UnterminatedBlockComment) {
    auto tokens = lex("/* unterminated");
    EXPECT_TRUE(has_errors());
}

// EOF
TEST_F(LexerTest, EofToken) {
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 1u);
    EXPECT_EQ(tokens[0].type, TokenType::Eof);
}

// Whitespace and newlines
TEST_F(LexerTest, WhitespaceHandling) {
    auto tokens = lex("  42   \n   7  ");
    ASSERT_GE(tokens.size(), 3u);
    EXPECT_EQ(tokens[0].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[0].value), 42);
    EXPECT_EQ(tokens[1].type, TokenType::IntegerLiteral);
    EXPECT_EQ(std::get<std::int64_t>(tokens[1].value), 7);
    EXPECT_EQ(tokens[2].type, TokenType::Eof);
}
