#include <gtest/gtest.h>
#include "parser/parser.h"
#include "lexer/lexer.h"
#include "common/diagnostic.h"

using namespace femto;

class ParserTest : public ::testing::Test {
protected:
    ast::Module parse(const std::string& source) {
        source_ = source;
        diag_ = std::make_unique<DiagnosticEngine>("test.femto", source_);
        Lexer lexer(source_, *diag_);
        tokens_ = lexer.tokenize();
        Parser parser(tokens_, *diag_);
        return parser.parse();
    }

    bool has_errors() const { return diag_->has_errors(); }
    std::size_t error_count() const { return diag_->error_count(); }

    template<typename T>
    const T* get_decl(const ast::Decl& decl) const {
        return std::get_if<T>(&decl.data);
    }

    template<typename T>
    const T* get_expr(const ast::Expr& expr) const {
        return std::get_if<T>(&expr.data);
    }

    template<typename T>
    const T* get_stmt(const ast::Stmt& stmt) const {
        return std::get_if<T>(&stmt.data);
    }

private:
    std::string source_;
    std::unique_ptr<DiagnosticEngine> diag_;
    std::vector<Token> tokens_;
};

// ---- Variable declarations ----

TEST_F(ParserTest, VariableDeclaration) {
    auto mod = parse("int32 x = 5;");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* var = get_decl<ast::VariableDecl>(*mod.decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
    EXPECT_FALSE(var->is_const);
}

TEST_F(ParserTest, ConstantDeclaration) {
    auto mod = parse("MAX_RETRIES :: uint32(5);");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* con = get_decl<ast::ConstantDecl>(*mod.decls[0]);
    ASSERT_NE(con, nullptr);
    EXPECT_EQ(con->name, "MAX_RETRIES");
}

TEST_F(ParserTest, ConstDeclaration) {
    auto mod = parse("const int32 x = compute();");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* var = get_decl<ast::VariableDecl>(*mod.decls[0]);
    ASSERT_NE(var, nullptr);
    EXPECT_EQ(var->name, "x");
    EXPECT_TRUE(var->is_const);
}

// ---- Function declarations ----

TEST_F(ParserTest, SimpleFunction) {
    auto mod = parse(R"(
add :: (int32 a, int32 b) -> int32
{
    return a + b;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "add");
    EXPECT_EQ(func->params.size(), 2u);
    EXPECT_FALSE(func->is_error_return);
    EXPECT_NE(func->body, nullptr);
}

TEST_F(ParserTest, FunctionWithDefaultArgs) {
    auto mod = parse(R"(
connect :: (string8 host, uint16 port = 80) -> !int32
{
    return success(0);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "connect");
    EXPECT_EQ(func->params.size(), 2u);
    EXPECT_TRUE(func->is_error_return);
    EXPECT_NE(func->params[1].default_value, nullptr);
}

TEST_F(ParserTest, GenericFunction) {
    auto mod = parse(R"(
max :: <T>(T a, T b) -> T
{
    if (a > b) then { return a; }
    else        { return b; }
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->name, "max");
    EXPECT_EQ(func->generic_params.size(), 1u);
    EXPECT_EQ(func->params.size(), 2u);
}

// ---- Struct declarations ----

TEST_F(ParserTest, StructDeclaration) {
    auto mod = parse(R"(
Point :: struct
{
    float32 x = 0.0;
    float32 y = 0.0;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* str = get_decl<ast::StructDecl>(*mod.decls[0]);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->name, "Point");
    EXPECT_EQ(str->fields.size(), 2u);
}

TEST_F(ParserTest, GenericStruct) {
    auto mod = parse(R"(
Pair :: struct <K, V>
{
    K key;
    V value;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* str = get_decl<ast::StructDecl>(*mod.decls[0]);
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->name, "Pair");
    EXPECT_EQ(str->generic_params.size(), 2u);
    EXPECT_EQ(str->fields.size(), 2u);
}

// ---- Enum declarations ----

TEST_F(ParserTest, EnumDeclaration) {
    auto mod = parse(R"(
Color :: enum -> uint8
{
    red = 1,
    green,
    blue,
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* en = get_decl<ast::EnumDecl>(*mod.decls[0]);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name, "Color");
    EXPECT_EQ(en->variants.size(), 3u);
}

// ---- Union declarations ----

TEST_F(ParserTest, UnionDeclaration) {
    auto mod = parse(R"(
Value :: union
{
    int64 i;
    float32 f;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* un = get_decl<ast::UnionDecl>(*mod.decls[0]);
    ASSERT_NE(un, nullptr);
    EXPECT_EQ(un->name, "Value");
    EXPECT_EQ(un->fields.size(), 2u);
}

// ---- Control flow ----

TEST_F(ParserTest, IfThenElse) {
    auto mod = parse(R"(
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
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    ASSERT_NE(func->body, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->stmts.size(), 1u);
    auto* if_stmt = get_stmt<ast::IfStmt>(*block->stmts[0]);
    ASSERT_NE(if_stmt, nullptr);
    EXPECT_NE(if_stmt->then_block, nullptr);
    EXPECT_NE(if_stmt->else_block, std::nullopt);
}

TEST_F(ParserTest, WhileLoop) {
    auto mod = parse(R"(
test :: () -> void
{
    while (true)
    {
        break;
    }
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->stmts.size(), 1u);
    auto* while_stmt = get_stmt<ast::WhileStmt>(*block->stmts[0]);
    ASSERT_NE(while_stmt, nullptr);
}

TEST_F(ParserTest, DoWhileLoop) {
    auto mod = parse(R"(
test :: () -> void
{
    do
    {
    } while (false);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    ASSERT_NE(block, nullptr);
    ASSERT_EQ(block->stmts.size(), 1u);
    auto* do_while = get_stmt<ast::DoWhileStmt>(*block->stmts[0]);
    ASSERT_NE(do_while, nullptr);
}

TEST_F(ParserTest, SwitchStatement) {
    auto mod = parse(R"(
test :: (Color c) -> void
{
    switch (c)
    {
        case Color::red   { }
        case Color::green { }
        default           { }
    }
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    ASSERT_NE(block, nullptr);
    auto* sw = get_stmt<ast::SwitchStmt>(*block->stmts[0]);
    ASSERT_NE(sw, nullptr);
    EXPECT_EQ(sw->cases.size(), 2u);
    EXPECT_NE(sw->default_case, std::nullopt);
}

TEST_F(ParserTest, BreakWithLevels) {
    auto mod = parse(R"(
test :: () -> void
{
    break(3);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* brk = get_stmt<ast::BreakStmt>(*block->stmts[0]);
    ASSERT_NE(brk, nullptr);
    EXPECT_EQ(brk->levels, 3u);
}

// ---- Error handling ----

TEST_F(ParserTest, ErrorReturnType) {
    auto mod = parse(R"(
read_config :: (string8 filename) -> !string8
{
    string8 data = std::io::read(filename)??;
    return success(data);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    EXPECT_TRUE(func->is_error_return);
}

TEST_F(ParserTest, SuccessExpr) {
    auto mod = parse(R"(
test :: () -> !int32
{
    return success(42);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    ASSERT_NE(ret, nullptr);
    ASSERT_NE(ret->value, std::nullopt);
    auto* success = get_expr<ast::SuccessExpr>(**ret->value);
    ASSERT_NE(success, nullptr);
}

TEST_F(ParserTest, FailureExpr) {
    auto mod = parse(R"(
test :: () -> !int32
{
    return failure(1);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    ASSERT_NE(func, nullptr);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    ASSERT_NE(ret, nullptr);
    auto* failure = get_expr<ast::FailureExpr>(**ret->value);
    ASSERT_NE(failure, nullptr);
}

// ---- Expressions ----

TEST_F(ParserTest, BinaryExpressions) {
    auto mod = parse(R"(
test :: () -> int32
{
    return 1 + 2 * 3;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    ASSERT_NE(ret, nullptr);
    // Should parse as 1 + (2 * 3) due to precedence
    auto* add = get_expr<ast::BinaryExpr>(**ret->value);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->op, ast::BinaryExpr::Add);
}

TEST_F(ParserTest, ComparisonExpressions) {
    auto mod = parse(R"(
test :: () -> bool8
{
    return 1 < 2;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* cmp = get_expr<ast::BinaryExpr>(**ret->value);
    ASSERT_NE(cmp, nullptr);
    EXPECT_EQ(cmp->op, ast::BinaryExpr::Lt);
}

TEST_F(ParserTest, CastExpression) {
    auto mod = parse(R"(
test :: () -> int64
{
    return int64(42);
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* cast = get_expr<ast::CastExpr>(**ret->value);
    ASSERT_NE(cast, nullptr);
    EXPECT_FALSE(cast->checked);
}

TEST_F(ParserTest, PointerDereference) {
    auto mod = parse(R"(
test :: (int32* p) -> int32
{
    return *p;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    EXPECT_EQ(func->params.size(), 1u);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* deref = get_expr<ast::UnaryExpr>(**ret->value);
    ASSERT_NE(deref, nullptr);
    EXPECT_EQ(deref->op, ast::UnaryExpr::Deref);
}

// ---- Namespaces ----

TEST_F(ParserTest, NamespaceDeclaration) {
    auto mod = parse(R"(
namespace std
{
    io :: struct { }
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* ns = get_decl<ast::NamespaceDecl>(*mod.decls[0]);
    ASSERT_NE(ns, nullptr);
    EXPECT_EQ(ns->name, "std");
    EXPECT_EQ(ns->decls.size(), 1u);
}

// ---- Extern ----

TEST_F(ParserTest, ExternBlock) {
    auto mod = parse(R"(
extern "C"
{
    write :: (int32 fd, uint8[] buf, uint64 count) -> int64;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* block = get_decl<ast::ExternBlock>(*mod.decls[0]);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->abi, "C");
    EXPECT_EQ(block->decls.size(), 1u);
}

// ---- Import ----

TEST_F(ParserTest, ImportDeclaration) {
    auto mod = parse("import std::io;");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* imp = get_decl<ast::ImportDecl>(*mod.decls[0]);
    ASSERT_NE(imp, nullptr);
    EXPECT_EQ(imp->path, "std::io");
}

TEST_F(ParserTest, ImportWithAlias) {
    auto mod = parse("import std::io as io;");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* imp = get_decl<ast::ImportDecl>(*mod.decls[0]);
    ASSERT_NE(imp, nullptr);
    EXPECT_EQ(imp->path, "std::io");
    EXPECT_NE(imp->alias, std::nullopt);
    EXPECT_EQ(*imp->alias, "io");
}

// ---- Literals ----

TEST_F(ParserTest, IntegerLiterals) {
    auto mod = parse(R"(
test :: () -> int32
{
    return 42;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* lit = get_expr<ast::IntegerLit>(**ret->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, 42);
}

TEST_F(ParserTest, FloatLiterals) {
    auto mod = parse(R"(
test :: () -> float64
{
    return 3.14;
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* lit = get_expr<ast::FloatLit>(**ret->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_DOUBLE_EQ(lit->value, 3.14);
}

TEST_F(ParserTest, StringLiterals) {
    auto mod = parse(R"(
test :: () -> string8
{
    return "hello";
}
)");
    ASSERT_EQ(mod.decls.size(), 1u);
    auto* func = get_decl<ast::FunctionDecl>(*mod.decls[0]);
    auto* block = get_stmt<ast::Block>(*func->body);
    auto* ret = get_stmt<ast::ReturnStmt>(*block->stmts[0]);
    auto* lit = get_expr<ast::StringLit>(**ret->value);
    ASSERT_NE(lit, nullptr);
    EXPECT_EQ(lit->value, "hello");
}

// ---- Multiple declarations ----

TEST_F(ParserTest, MultipleDeclarations) {
    auto mod = parse(R"(
int32 x = 1;
int32 y = 2;
add :: (int32 a, int32 b) -> int32 { return a + b; }
)");
    EXPECT_EQ(mod.decls.size(), 3u);
    EXPECT_TRUE(get_decl<ast::VariableDecl>(*mod.decls[0]) != nullptr);
    EXPECT_TRUE(get_decl<ast::VariableDecl>(*mod.decls[1]) != nullptr);
    EXPECT_TRUE(get_decl<ast::FunctionDecl>(*mod.decls[2]) != nullptr);
}

// ---- Error cases ----

TEST_F(ParserTest, ErrorOnMissingSemicolon) {
    auto mod = parse("int32 x = 5");
    // Parser tolerates missing semicolons gracefully
    EXPECT_EQ(mod.decls.size(), 1u);
}
