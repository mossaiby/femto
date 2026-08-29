#include "test_framework.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include "common/arena.hpp"

using namespace femto;

TEST_CASE(Sema, PrimitiveTypeSizesAndAlignments) {
    Arena arena;
    SourceManager sm("test.femto", "");
    Diagnostics diag(sm);
    TypeChecker tc(arena, diag);

    ASTProgram empty_prog;
    tc.check_program(empty_prog);

    const auto& env = tc.type_env();
    ASSERT_EQ(env.at("int8")->size_bytes, 1u);
    ASSERT_EQ(env.at("int16")->size_bytes, 2u);
    ASSERT_EQ(env.at("int32")->size_bytes, 4u);
    ASSERT_EQ(env.at("int64")->size_bytes, 8u);
    ASSERT_EQ(env.at("int128")->size_bytes, 16u);
    ASSERT_EQ(env.at("float32")->size_bytes, 4u);
    ASSERT_EQ(env.at("float64")->size_bytes, 8u);
    ASSERT_EQ(env.at("char16")->size_bytes, 2u);
    ASSERT_EQ(env.at("char32")->size_bytes, 4u);
}

TEST_CASE(Sema, StructMemoryLayoutCalculation) {
    std::string src =
        "PaddedStruct :: struct {\n"
        "    int8  a = 0;\n"
        "    int64 b = 0;\n"
        "    int32 c = 0;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    ASSERT_FALSE(diag.has_errors());

    TypeChecker tc(arena, diag);
    bool ok = tc.check_program(prog);
    ASSERT_TRUE(ok);

    auto* st_type = tc.type_env().at("PaddedStruct");
    ASSERT_TRUE(st_type != nullptr);
    ASSERT_EQ(st_type->align_bytes, 8u);
    ASSERT_EQ(st_type->size_bytes, 24u); // 1 + 7(pad) + 8 + 4 + 4(pad) = 24

    const auto& s_info = std::get<StructTypeInfo>(st_type->data);
    ASSERT_EQ(s_info.fields[0].offset, 0u);
    ASSERT_EQ(s_info.fields[1].offset, 8u);
    ASSERT_EQ(s_info.fields[2].offset, 16u);
}

TEST_CASE(Sema, UnionLayoutCalculation) {
    std::string src =
        "DataUnion :: union {\n"
        "    int32 a;\n"
        "    int64 b;\n"
        "    float32 c;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    bool ok = tc.check_program(prog);
    ASSERT_TRUE(ok);

    auto* un_type = tc.type_env().at("DataUnion");
    ASSERT_TRUE(un_type != nullptr);
    ASSERT_EQ(un_type->size_bytes, 8u); // max(4, 8, 4) = 8
    ASSERT_EQ(un_type->align_bytes, 8u);
}

TEST_CASE(Sema, GenericMonomorphization) {
    std::string src =
        "Pair :: struct <K, V> {\n"
        "    K key;\n"
        "    V value;\n"
        "}\n"
        "choose :: <T>(T a, T b) -> T {\n"
        "    return a;\n"
        "}\n"
        "main :: () -> int32 {\n"
        "    Pair<int32, float64> p = { .key = 1, .value = 2.5 };\n"
        "    int32 c = choose<int32>(10, 20);\n"
        "    return c;\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    bool ok = tc.check_program(prog);
    ASSERT_TRUE(ok);

    // Monomorphized struct Pair__int32__float64 should exist in type_env
    auto it = tc.type_env().find("Pair__int32__float64");
    ASSERT_TRUE(it != tc.type_env().end());
    ASSERT_EQ(it->second->size_bytes, 16u);

    // Monomorphized function choose__int32 should be appended to prog.functions
    bool found_fn = false;
    for (const auto* fn : prog.functions) {
        if (fn->name == "choose__int32") found_fn = true;
    }
    ASSERT_TRUE(found_fn);
}

TEST_CASE(Sema, CompileTimeConstEval) {
    std::string src =
        "A :: 10 + 20 * 2;\n"
        "B :: @sizeof(int128);\n"
        "C :: @alignof(int64);\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    bool ok = tc.check_program(prog);
    ASSERT_TRUE(ok);

    const auto& c_defs = tc.const_defs();
    ASSERT_EQ(c_defs.at("A"), 50);
    ASSERT_EQ(c_defs.at("B"), 16);
    ASSERT_EQ(c_defs.at("C"), 8);
}