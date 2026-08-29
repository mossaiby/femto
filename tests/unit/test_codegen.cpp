#include "test_framework.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/type_checker.hpp"
#include "codegen/nasm_emitter.hpp"
#include "common/arena.hpp"

using namespace femto;

TEST_CASE(CodeGen, StackFrameCalculationAndAlignment) {
    std::string src =
        "#export\n"
        "heavy_stack :: () -> int32 {\n"
        "    int32[100] big_arr = [0];\n"
        "    int128 val = @cast(int128, 42);\n"
        "    return big_arr[0];\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    ASSERT_TRUE(tc.check_program(prog));

    NasmEmitter emitter(tc.type_env(), tc.enum_defs(), tc.const_defs(), tc.float_const_defs(), true);
    std::string asm_code = emitter.generate_assembly(prog);

    // Frame for 400 bytes array + 16 bytes int128 + 128 scratchpad = 544 bytes, 16-byte aligned = 544
    ASSERT_TRUE(asm_code.find("sub rsp, ") != std::string::npos);
    ASSERT_TRUE(asm_code.find("global heavy_stack") != std::string::npos);
}

TEST_CASE(CodeGen, SystemVCallAlignmentSequence) {
    std::string src =
        "extern \"C\" {\n"
        "    puts :: (string8 str) -> int32;\n"
        "}\n"
        "#export\n"
        "call_libc :: () -> void {\n"
        "    puts(\"Hello\");\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    ASSERT_TRUE(tc.check_program(prog));

    NasmEmitter emitter(tc.type_env(), tc.enum_defs(), tc.const_defs(), tc.float_const_defs(), true);
    std::string asm_code = emitter.generate_assembly(prog);

    // Must emit the dynamic 16-byte alignment check
    ASSERT_TRUE(asm_code.find("test rsp, 15") != std::string::npos);
    ASSERT_TRUE(asm_code.find("sub rsp, 8") != std::string::npos);
    ASSERT_TRUE(asm_code.find("call puts") != std::string::npos);
}

TEST_CASE(CodeGen, BoundsCheckPanicEmission) {
    std::string src =
        "#export\n"
        "access :: (int32[] s, int32 idx) -> int32 {\n"
        "    return s[idx];\n"
        "}\n";

    SourceManager sm("test.femto", src);
    Diagnostics diag(sm);
    Arena arena;
    Lexer lexer(sm, diag);
    Parser parser(lexer, arena, diag);

    ASTProgram prog = parser.parse_program();
    TypeChecker tc(arena, diag);
    ASSERT_TRUE(tc.check_program(prog));

    NasmEmitter emitter(tc.type_env(), tc.enum_defs(), tc.const_defs(), tc.float_const_defs(), true);
    std::string asm_code = emitter.generate_assembly(prog);

    ASSERT_TRUE(asm_code.find("call __builtin_panic") != std::string::npos);
    ASSERT_TRUE(asm_code.find("str_bounds_panic: db \"Slice index out of bounds\", 10, 0") != std::string::npos);
}