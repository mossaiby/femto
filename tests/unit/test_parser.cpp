#include "test_framework.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "common/arena.hpp"

using namespace femto;

TEST_CASE(Parser, BasicTypeParsing) {
    std::string src =
        "test_fn :: () -> void {\n"
        "    int32 x = 5;\n"
        "    int64* p = null;\n"
        "    float32[4] arr = [1.0, 2.0, 3.0, 4.0];\n"
        "    int32[] s = {};\n"
        "    !string8 res = success(\"ok\");\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());
    ASSERT_EQ(prog.functions.size(), 1u);
    ASSERT_EQ(prog.functions[0]->body.size(), 5u);
}

TEST_CASE(Parser, FunctionDeclarations) {
    std::string src = 
        "#export\n"
        "add :: (int32 a, int32 b = 10) -> int32 {\n"
        "    return a + b;\n"
        "}\n"
        "identity :: <T>(T x) -> T {\n"
        "    return x;\n"
        "}\n"
        "extern \"C\" {\n"
        "    printf :: (string8 fmt, ...) -> int32;\n"
        "}\n"
        "my_print :: (string8 fmt, any... args) -> void {\n"
        "    return;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());
    ASSERT_EQ(prog.functions.size(), 4u);

    auto* fn1 = prog.functions[0];
    ASSERT_STREQ(fn1->name, "add");
    ASSERT_TRUE(fn1->is_exported);
    ASSERT_FALSE(fn1->is_variadic);
    ASSERT_FALSE(fn1->has_variadic_slice);
    ASSERT_EQ(fn1->params.size(), 2u);

    auto* fn2 = prog.functions[1];
    ASSERT_STREQ(fn2->name, "identity");
    ASSERT_EQ(fn2->generic_params.size(), 1u);
    ASSERT_STREQ(fn2->generic_params[0], "T");

    auto* fn3 = prog.functions[2];
    ASSERT_STREQ(fn3->name, "printf");
    ASSERT_TRUE(fn3->is_extern_c);
    ASSERT_TRUE(fn3->is_variadic);
    ASSERT_FALSE(fn3->has_variadic_slice);

    auto* fn4 = prog.functions[3];
    ASSERT_STREQ(fn4->name, "my_print");
    ASSERT_FALSE(fn4->is_extern_c);
    ASSERT_TRUE(fn4->has_variadic_slice);
    ASSERT_EQ(fn4->params.size(), 2u);
    ASSERT_TRUE(fn4->params[1].is_variadic_slice);
}

TEST_CASE(Parser, StructAndUnionParsing) {
    std::string src =
        "Point :: struct {\n"
        "    int32 x = 0;\n"
        "    int32 y = 0;\n"
        "}\n"
        "Holder :: union <T> {\n"
        "    T val;\n"
        "    int64 bits;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());
    ASSERT_EQ(prog.structs.size(), 1u);
    ASSERT_EQ(prog.unions.size(), 1u);

    ASSERT_STREQ(prog.structs[0]->name, "Point");
    ASSERT_EQ(prog.structs[0]->fields.size(), 2u);

    ASSERT_STREQ(prog.unions[0]->name, "Holder");
    ASSERT_EQ(prog.unions[0]->generic_params.size(), 1u);
    ASSERT_EQ(prog.unions[0]->fields.size(), 2u);
}

TEST_CASE(Parser, OperatorPrecedencePratt) {
    std::string src =
        "compute :: () -> int32 {\n"
        "    return 1 + 2 * 3 == 7 && 4 <= 5;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());

    auto* ret_stmt = prog.functions[0]->body[0];
    ASSERT_EQ(ret_stmt->kind, StmtKind::Return);

    // Root binary must be '&&' (precedence 3)
    auto* root_expr = ret_stmt->value_expr;
    ASSERT_EQ(root_expr->kind, ExprKind::Binary);
    ASSERT_STREQ(root_expr->op, "&&");

    // Left child is '==' (precedence 7)
    auto* left_cmp = root_expr->left;
    ASSERT_EQ(left_cmp->kind, ExprKind::Binary);
    ASSERT_STREQ(left_cmp->op, "==");

    // Left child of left_cmp is '+' (precedence 10)
    auto* add_expr = left_cmp->left;
    ASSERT_EQ(add_expr->kind, ExprKind::Binary);
    ASSERT_STREQ(add_expr->op, "+");

    // Right child of add_expr is '*' (precedence 11)
    auto* mul_expr = add_expr->right;
    ASSERT_EQ(mul_expr->kind, ExprKind::Binary);
    ASSERT_STREQ(mul_expr->op, "*");
}

TEST_CASE(Parser, ControlFlowStatements) {
    std::string src =
        "flow :: (int32 c) -> void {\n"
        "    if (c > 0) then { return; } else { return; }\n"
        "    while (c < 10) { c++; }\n"
        "    do { c--; } while (c > 0);\n"
        "    for (int32 i = 0; i < 5; i++) { break(2); }\n"
        "    foreach (int32 idx, int32 v in [1, 2, 3]) { continue; }\n"
        "    match (c) {\n"
        "        # == 1 { 10 }\n"
        "        default { 0 }\n"
        "    };\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());
    ASSERT_EQ(prog.functions.size(), 1u);
    ASSERT_EQ(prog.functions[0]->body.size(), 6u);
}