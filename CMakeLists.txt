cmake_minimum_required(VERSION 3.21)
project(quebratsk_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# Automatically fetch godot-cpp bindings for Godot 4.3
FetchContent_Declare(
    godot-cpp
    GIT_REPOSITORY https://github.com/godotengine/godot-cpp.git
    GIT_TAG godot-4.3-stable
)
FetchContent_MakeAvailable(godot-cpp)

# Source files
file(GLOB_RECURSE QUEBRATSK_SOURCES 
    "src/*.cpp"
    "src/*.c"
)
file(GLOB_RECURSE QUEBRATSK_HEADERS 
    "src/*.h"
    "src/*.hpp"
)

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

set_target_properties(quebratsk_engine PROPERTIES
    OUTPUT_NAME "libquebratsk.windows.debug.x86_64"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/demo/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/demo/bin"
)
