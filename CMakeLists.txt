cmake_minimum_required(VERSION 3.21)
project(quebratsk_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# NOTE ON DEBUG VS RELEASE BUILDS
#
# godot-cpp 4.3's CMakeLists reads CMAKE_BUILD_TYPE, which is a *single-config*
# variable. Under the Visual Studio (multi-config) generator it is empty at configure
# time, so godot-cpp defaults itself to Debug, bakes /MDd into every configuration and
# always names its library godot-cpp.windows.debug.64.lib. As a result
# `cmake --build build --config Release` links release-CRT objects against a debug-CRT
# godot-cpp and fails with unresolved __imp__CrtDbgReport / __imp__calloc_dbg.
#
# Each configuration therefore needs its OWN build tree, configured up front:
#
#   cmake -S . -B build          -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build          --config Debug
#
#   cmake -S . -B build-release  -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-release  --config Release
#
# Do not build both configurations out of a single tree.

include(FetchContent)

# Automatically fetch godot-cpp bindings for Godot 4.3
FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG godot-4.3-stable
)
FetchContent_MakeAvailable(godot-cpp)

# Source files
file(GLOB_RECURSE QUEBRATSK_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.c"
)
file(GLOB_RECURSE QUEBRATSK_HEADERS CONFIGURE_DEPENDS
    "src/*.h"
    "src/*.hpp"
)

# The CLI tool defines main(); it must not be linked into the shared library.
list(REMOVE_ITEM QUEBRATSK_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp")

# Build dynamic library
add_library(quebratsk_engine SHARED ${QUEBRATSK_SOURCES} ${QUEBRATSK_HEADERS})

if(MSVC)
    target_compile_options(quebratsk_engine PRIVATE /FS)
endif()

target_include_directories(quebratsk_engine PUBLIC
    src/
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(quebratsk_engine PRIVATE godot::cpp)

# The output name must follow the build configuration. It used to be hardcoded to
# "debug", so a Release build silently overwrote the debug DLL and the .gdextension
# ended up shipping a template_debug binary as the release artifact.
set(QB_CFG_SUFFIX "$<IF:$<CONFIG:Debug>,debug,release>")

# Visual Studio is a multi-config generator, so it appends the configuration name to
# *_OUTPUT_DIRECTORY: the DLL landed in demo/bin/Debug/ while the .gdextension points at
# demo/bin/. Godot went on loading whatever stale binary was left at the top level, so
# rebuilds appeared to change nothing at all. Wrapping the path in a generator expression
# is the documented way to suppress that per-config subdirectory.
set(QB_OUTPUT_DIR "$<1:${CMAKE_SOURCE_DIR}/demo/bin>")

set_target_properties(quebratsk_engine PROPERTIES
    OUTPUT_NAME "libquebratsk.windows.${QB_CFG_SUFFIX}.x86_64"
    LIBRARY_OUTPUT_DIRECTORY "${QB_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${QB_OUTPUT_DIR}"
)

# ------------------------------------------------------------------------ tests ----
#
# Each suite is a plain executable that prints its checks and exits non-zero on failure.
# No framework: the point is that `ctest` in CI and a developer running the binary by hand
# see exactly the same output.
#
# These were previously buildable only by copying a hand-written `cl` command line out of
# a comment at the top of each file — five different command lines, none of them checked
# by anything. Nothing stopped one from silently rotting.
option(QUEBRATSK_BUILD_TESTS "Build the parser test suites" ON)

if(QUEBRATSK_BUILD_TESTS)
    enable_testing()

    function(quebratsk_add_test name)
        add_executable(${name} "tests/${name}.cpp" ${ARGN})
        target_include_directories(${name} PRIVATE
            src
            src/core/image   # dxt_decoder_test includes "dxt_decoder.h" unqualified
            ${CMAKE_CURRENT_SOURCE_DIR}
        )
        # Even the header-only suites link godot-cpp: the IR types they exercise use
        # godot::Vector3 and friends.
        target_link_libraries(${name} PRIVATE godot::cpp)
        set_target_properties(${name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests>")
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    quebratsk_add_test(byte_reader_test)
    quebratsk_add_test(dxt_decoder_test  src/core/image/dxt_decoder.cpp)
    quebratsk_add_test(anim_decoder_test src/parsers/source1/anim_decoder.cpp)
    quebratsk_add_test(mdl10_parser_test src/parsers/goldsrc/mdl10_parser.cpp)
    quebratsk_add_test(paa_parser_test
        src/parsers/rv_enfusion/paa_parser.cpp
        src/core/image/dxt_decoder.cpp)
    quebratsk_add_test(source_mdl_test
        src/parsers/source1/mdl_source_parser.cpp
        src/parsers/source1/vvd_parser.cpp
        src/parsers/source1/anim_decoder.cpp)
endif()
