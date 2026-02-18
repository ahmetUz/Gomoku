include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# Collect all test files
file(GLOB TEST_SOURCES test_*.cpp)

# Create test executable
add_executable(gomoku_tests ${TEST_SOURCES})
target_link_libraries(gomoku_tests PRIVATE gomoku_lib Catch2::Catch2WithMain)

# Register with CTest
include(Catch)
catch_discover_tests(gomoku_tests)
