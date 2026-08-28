cmake_minimum_required(VERSION 3.20)
project(femtoc CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(src)

add_executable(femtoc
    src/main.cpp
    src/frontend/lexer.cpp
    src/frontend/parser.cpp
    src/sema/type_checker.cpp
    src/codegen/nasm_emitter.cpp
)