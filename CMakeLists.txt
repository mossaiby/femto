cmake_minimum_required(VERSION 3.20)
project(femtoc CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directory for compiler headers
include_directories(src tests/unit)

# Platform-specific runtime assembly configuration
if(WIN32 OR MSVC)
    set(RUNTIME_ASM ${CMAKE_CURRENT_SOURCE_DIR}/runtime/femto_rt_windows.asm)
    set(RUNTIME_OBJ ${CMAKE_CURRENT_BINARY_DIR}/femto_rt.obj)
    set(NASM_FORMAT win64)
else()
    set(RUNTIME_ASM ${CMAKE_CURRENT_SOURCE_DIR}/runtime/femto_rt_linux.asm)
    set(RUNTIME_OBJ ${CMAKE_CURRENT_BINARY_DIR}/femto_rt.o)
    set(NASM_FORMAT elf64)
endif()

# Assemble runtime assembly using NASM
add_custom_command(
    OUTPUT ${RUNTIME_OBJ}
    COMMAND nasm -f ${NASM_FORMAT} ${RUNTIME_ASM} -o ${RUNTIME_OBJ}
    DEPENDS ${RUNTIME_ASM}
    COMMENT "Assembling Femto runtime (${NASM_FORMAT}): ${RUNTIME_ASM}"
)

# Custom target ensuring runtime object is always built
add_custom_target(femto_runtime ALL DEPENDS ${RUNTIME_OBJ})

# Main compiler executable with LSP support
add_executable(femtoc
    src/main.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/sema/type_checker.cpp
    src/codegen/nasm_emitter.cpp
    src/lsp/lsp_server.cpp
)

# Make compiler binary depend on runtime object build
add_dependencies(femtoc femto_runtime)

# Inject stdlib and runtime paths directly into the compiler binary
target_compile_definitions(femtoc PRIVATE
    FEMTO_RUNTIME_OBJ="${RUNTIME_OBJ}"
    FEMTO_STDLIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/stdlib"
)

# Compiler Components Native Unit Test Executable
add_executable(femto_unit_tests
    tests/unit/main.cpp
    tests/unit/test_lexer.cpp
    tests/unit/test_parser.cpp
    tests/unit/test_sema.cpp
    tests/unit/test_codegen.cpp
    tests/unit/test_lsp.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/sema/type_checker.cpp
    src/codegen/nasm_emitter.cpp
    src/lsp/lsp_server.cpp
)

add_dependencies(femto_unit_tests femto_runtime)

# Enable CTest integration and custom unified 'check' target
enable_testing()
find_package(Python3 REQUIRED)

add_custom_target(check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_runner.py
    DEPENDS femtoc femto_unit_tests femto_runtime
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Executing Femto Compiler Full-Blown Unified Test Suite"
)