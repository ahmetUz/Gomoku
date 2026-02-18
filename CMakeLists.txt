cmake_minimum_required(VERSION 3.14)
project(gomoku LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
add_compile_options(-Wall -Wextra -Wpedantic)

# Release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Library target (all source except main.cpp)
file(GLOB_RECURSE LIB_SOURCES
    src/board/*.cpp
    src/rules/*.cpp
    src/eval/*.cpp
    src/search/*.cpp
    src/engine/*.cpp
)

add_library(gomoku_lib STATIC ${LIB_SOURCES})
target_include_directories(gomoku_lib PUBLIC include)
target_link_libraries(gomoku_lib PUBLIC pthread)

# Executable target
add_executable(gomoku src/main.cpp)
target_link_libraries(gomoku PRIVATE gomoku_lib)

# Tests (optional, enabled by default)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
