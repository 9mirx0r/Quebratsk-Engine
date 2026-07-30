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
