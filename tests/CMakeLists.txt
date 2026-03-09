include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)

# Fetch Catch2 source but add it with EXCLUDE_FROM_ALL so its 107 compilation
# units are NOT built by a bare `make`.  They only compile when a target that
# depends on Catch2 (i.e. gomoku_tests) is explicitly requested.
FetchContent_GetProperties(Catch2)
if(NOT catch2_POPULATED)
    FetchContent_Populate(Catch2)
    add_subdirectory(${catch2_SOURCE_DIR} ${catch2_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Collect all test files
file(GLOB TEST_SOURCES test_*.cpp)

# Test executable — EXCLUDE_FROM_ALL: only built via `make gomoku_tests`
add_executable(gomoku_tests EXCLUDE_FROM_ALL ${TEST_SOURCES})
target_link_libraries(gomoku_tests PRIVATE gomoku_lib Catch2::Catch2WithMain)

# Register with CTest (tests still discoverable by `ctest` after building)
include(Catch)
catch_discover_tests(gomoku_tests)
