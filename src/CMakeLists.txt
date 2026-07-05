add_executable(femto
  main.cpp
  common/diagnostic.cpp
  common/string_pool.cpp
  lexer/token.cpp
  lexer/lexer.cpp
  parser/parser.cpp
  sema/type_system.cpp
  sema/symbol_table.cpp
  sema/type_checker.cpp
  hir/ast_to_hir.cpp
  hir/hir_optimizer.cpp
  lir/lir_optimizer.cpp
  codegen/x86_64/x86_64.cpp
)

target_include_directories(femto PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Tests
add_executable(femto_tests
  common/diagnostic.cpp
  common/string_pool.cpp
  lexer/token.cpp
  lexer/lexer.cpp
  parser/parser.cpp
  sema/type_system.cpp
  sema/symbol_table.cpp
  sema/type_checker.cpp
  hir/ast_to_hir.cpp
  hir/hir_optimizer.cpp
  lir/lir_optimizer.cpp
  codegen/x86_64/x86_64.cpp
  common/diagnostic.test.cpp
  lexer/lexer.test.cpp
  parser/parser.test.cpp
  sema/sema.test.cpp
  examples/examples.test.cpp
)

target_include_directories(femto_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Set the examples directory path for the examples test
target_compile_definitions(femto_tests PRIVATE
  EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples"
)

target_link_libraries(femto_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(femto_tests)
