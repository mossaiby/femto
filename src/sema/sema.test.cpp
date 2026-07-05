#include <gtest/gtest.h>
#include "sema/type_checker.h"
#include "parser/parser.h"
#include "lexer/lexer.h"
#include "common/diagnostic.h"

using namespace femto;

class SemaTest : public ::testing::Test {
protected:
    bool check(const std::string& source) {
        source_ = source;
        diag_ = std::make_unique<DiagnosticEngine>("test.femto", source_);
        Lexer lexer(source_, *diag_);
        tokens_ = lexer.tokenize();
        Parser parser(tokens_, *diag_);
        auto mod = parser.parse();
        if (diag_->has_errors()) return false;
        sema::TypeChecker checker(*diag_);
        return checker.check(mod);
    }

    bool has_errors() const { return diag_->has_errors(); }
    std::size_t error_count() const { return diag_->error_count(); }

private:
    std::string source_;
    std::unique_ptr<DiagnosticEngine> diag_;
    std::vector<Token> tokens_;
};

// ---- Variable declarations ----

TEST_F(SemaTest, VariableDecl) {
    EXPECT_TRUE(check(R"(
test :: () -> void
{
    int32 x = 5;
}
)"));
}

TEST_F(SemaTest, VariableTypeMismatch) {
    EXPECT_FALSE(check(R"(
test :: () -> void
{
    int32 x = true;
}
)"));
}

// ---- Constants ----

TEST_F(SemaTest, ConstantDecl) {
    EXPECT_TRUE(check(R"(
MAX :: int32(5);
PI :: float64(3.14);
)"));
}

// ---- Functions ----

TEST_F(SemaTest, SimpleFunction) {
    EXPECT_TRUE(check(R"(
add :: (int32 a, int32 b) -> int32
{
    return a + b;
}
)"));
}

TEST_F(SemaTest, FunctionReturnTypeMismatch) {
    EXPECT_FALSE(check(R"(
test :: () -> int32
{
    return true;
}
)"));
}

TEST_F(SemaTest, UndefinedIdentifier) {
    EXPECT_FALSE(check(R"(
test :: () -> int32
{
    return x;
}
)"));
}

// ---- Arithmetic ----

TEST_F(SemaTest, IntegerArithmetic) {
    EXPECT_TRUE(check(R"(
test :: () -> int32
{
    int32 a = 1 + 2 * 3;
    return a;
}
)"));
}

TEST_F(SemaTest, StringArithmeticError) {
    EXPECT_FALSE(check(R"(
test :: () -> void
{
    string8 a = "hello" + 5;
}
)"));
}

// ---- Comparisons ----

TEST_F(SemaTest, Comparisons) {
    EXPECT_TRUE(check(R"(
test :: () -> void
{
    bool8 x = 1 < 2;
    bool8 y = 3 == 4;
}
)"));
}

// ---- Pointers ----

TEST_F(SemaTest, PointerDereference) {
    EXPECT_TRUE(check(R"(
test :: (int32* p) -> int32
{
    return *p;
}
)"));
}

// ---- Control flow ----

TEST_F(SemaTest, IfThenElse) {
    EXPECT_TRUE(check(R"(
test :: (int32 x) -> int32
{
    if (x > 0) then
    {
        return 1;
    }
    else
    {
        return 0;
    }
}
)"));
}

TEST_F(SemaTest, WhileLoop) {
    EXPECT_TRUE(check(R"(
test :: () -> void
{
    while (true)
    {
        break;
    }
}
)"));
}

TEST_F(SemaTest, DoWhileLoop) {
    EXPECT_TRUE(check(R"(
test :: () -> void
{
    do
    {
    } while (false);
}
)"));
}

// ---- Namespaces ----

TEST_F(SemaTest, NamespaceDecl) {
    EXPECT_TRUE(check(R"(
namespace std
{
    x :: int32(5);
}
)"));
}

// ---- Structs/Enums ----

TEST_F(SemaTest, StructDecl) {
    EXPECT_TRUE(check(R"(
Point :: struct
{
    float32 x = 0.0;
    float32 y = 0.0;
}
)"));
}

TEST_F(SemaTest, EnumDecl) {
    EXPECT_TRUE(check(R"(
Color :: enum -> uint8
{
    red = 1,
    green,
    blue,
}
)"));
}

// ---- Error returns ----

TEST_F(SemaTest, ErrorReturnType) {
    EXPECT_TRUE(check(R"(
test :: () -> !int32
{
    return success(42);
}
)"));
}

// ---- Multiple declarations ----

TEST_F(SemaTest, MultipleDecl) {
    EXPECT_TRUE(check(R"(
int32 x = 1;
int32 y = 2;
add :: (int32 a, int32 b) -> int32 { return a + b; }
)"));
}
