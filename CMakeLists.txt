cmake_minimum_required(VERSION 3.30)
project(femto LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(src)

# Examples directory - list all .femto files
file(GLOB FEMTO_EXAMPLES "${CMAKE_SOURCE_DIR}/examples/*.femto")
message(STATUS "Found ${CMAKE_CURRENT_LIST_FILE} femto examples: ${FEMTO_EXAMPLES}")
