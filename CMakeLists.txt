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

# GUI target (optional, requires SFML)
option(BUILD_GUI "Build SFML GUI" ON)
if(BUILD_GUI)
    # --- Local deps fallback (deps/ ships headers + lib stubs) ---------------
    set(LOCAL_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps")
    if(EXISTS "${LOCAL_DEPS_DIR}/include/GL/gl.h")
        set(LOCAL_DEPS_INC "${LOCAL_DEPS_DIR}/include")
        set(LOCAL_DEPS_LIB "${LOCAL_DEPS_DIR}/lib")

        # Help FindOpenGL
        if(NOT DEFINED OPENGL_INCLUDE_DIR)
            set(OPENGL_INCLUDE_DIR "${LOCAL_DEPS_INC}" CACHE PATH "" FORCE)
        endif()
        if(NOT DEFINED OPENGL_opengl_LIBRARY)
            set(OPENGL_opengl_LIBRARY "${LOCAL_DEPS_LIB}/libOpenGL.so" CACHE FILEPATH "" FORCE)
        endif()
        if(NOT DEFINED OPENGL_glx_LIBRARY)
            set(OPENGL_glx_LIBRARY "${LOCAL_DEPS_LIB}/libGLX.so" CACHE FILEPATH "" FORCE)
        endif()
        # Help FindFreetype
        if(NOT DEFINED FREETYPE_INCLUDE_DIR_ft2build)
            set(FREETYPE_INCLUDE_DIR_ft2build "${LOCAL_DEPS_INC}/freetype2" CACHE PATH "" FORCE)
            set(FREETYPE_INCLUDE_DIR_freetype2 "${LOCAL_DEPS_INC}/freetype2" CACHE PATH "" FORCE)
        endif()
        if(NOT DEFINED FREETYPE_LIBRARY)
            set(FREETYPE_LIBRARY "${LOCAL_DEPS_LIB}/libfreetype.so" CACHE FILEPATH "" FORCE)
        endif()
        # Help FindUDev (SFML custom module)
        if(NOT DEFINED UDEV_INCLUDE_DIR)
            set(UDEV_INCLUDE_DIR "${LOCAL_DEPS_INC}" CACHE PATH "" FORCE)
        endif()
        if(NOT DEFINED UDEV_LIBRARIES)
            set(UDEV_LIBRARIES "${LOCAL_DEPS_LIB}/libudev.so" CACHE FILEPATH "" FORCE)
        endif()
        # X11 extensions (Xrandr, Xcursor, …) — needed for static link
        if(NOT DEFINED X11_Xrandr_LIB)
            set(X11_Xrandr_LIB  "${LOCAL_DEPS_LIB}/libXrandr.so"  CACHE FILEPATH "" FORCE)
            set(X11_Xcursor_LIB "${LOCAL_DEPS_LIB}/libXcursor.so" CACHE FILEPATH "" FORCE)
            set(X11_Xi_LIB      "${LOCAL_DEPS_LIB}/libXi.so"      CACHE FILEPATH "" FORCE)
            set(X11_Xfixes_LIB  "${LOCAL_DEPS_LIB}/libXfixes.so"  CACHE FILEPATH "" FORCE)
            set(X11_Xrender_LIB "${LOCAL_DEPS_LIB}/libXrender.so" CACHE FILEPATH "" FORCE)
        endif()

        # Extra include path so SFML sources find X11 extension headers
        add_compile_options(-isystem "${LOCAL_DEPS_INC}")
        link_directories("${LOCAL_DEPS_LIB}")
    endif()

    # --- SFML ----------------------------------------------------------------
    find_package(SFML 2.5 COMPONENTS graphics window system QUIET)
    if(NOT SFML_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            sfml
            URL https://github.com/SFML/SFML/archive/refs/tags/2.6.1.tar.gz
        )
        set(SFML_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
        set(SFML_BUILD_NETWORK OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(sfml)
    endif()

    file(GLOB GUI_SOURCES src/gui/*.cpp)
    add_executable(gomoku_gui ${GUI_SOURCES})
    target_link_libraries(gomoku_gui PRIVATE gomoku_lib sfml-graphics sfml-window sfml-system)

    # When building SFML from source as static, link X11 deps explicitly
    if(NOT SFML_FOUND AND NOT BUILD_SHARED_LIBS)
        find_package(X11 REQUIRED)
        find_package(OpenGL REQUIRED)
        target_link_libraries(gomoku_gui PRIVATE
            ${X11_LIBRARIES} ${X11_Xrandr_LIB} ${X11_Xcursor_LIB}
            ${X11_Xi_LIB} ${X11_Xfixes_LIB} ${X11_Xrender_LIB}
            ${OPENGL_opengl_LIBRARY} ${OPENGL_glx_LIBRARY}
            ${FREETYPE_LIBRARY} ${UDEV_LIBRARIES}
        )
    endif()

    # Copy assets next to binary
    add_custom_command(TARGET gomoku_gui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:gomoku_gui>/assets
    )
endif()

# Benchmark target
add_executable(gomoku_bench bench/benchmark.cpp)
target_link_libraries(gomoku_bench PRIVATE gomoku_lib)

# Tests (optional, enabled by default)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
