cmake_minimum_required(VERSION 3.16)

project(miniDB
    VERSION 1.0.0
    DESCRIPTION "A lightweight file-based relational database engine in C++"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standard / compiler settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put binaries in build/<config>/ so the README's `.\build\Release\minidb.exe`
# path works out of the box with the MSVC multi-config generator.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
    # Silence the "use _CRT_SECURE_NO_WARNINGS" noise from the standard headers.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Engine library (everything except the CLI entry point)
# ---------------------------------------------------------------------------
add_library(minidb_engine STATIC
    src/utils.cpp
    src/lexer.cpp
    src/parser.cpp
    src/pager.cpp
    src/table.cpp
    src/bplustree.cpp
    src/catalog.cpp
    src/database.cpp
    src/executor.cpp
    src/cli.cpp
)

target_include_directories(minidb_engine PUBLIC ${CMAKE_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
add_executable(minidb src/main.cpp)
target_link_libraries(minidb PRIVATE minidb_engine)

# ---------------------------------------------------------------------------
# Tests (optional): built with the same engine library.
# Run with `ctest` from the build directory, or launch the exe directly.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(minidb_tests tests/test_basic.cpp)
target_link_libraries(minidb_tests PRIVATE minidb_engine)
add_test(NAME minidb_tests COMMAND minidb_tests)
