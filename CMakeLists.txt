cmake_minimum_required(VERSION 3.21)
project(quebratsk_engine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Source files
file(GLOB_RECURSE QUEBRATSK_SOURCES 
    "src/*.cpp"
    "src/*.c"
)
file(GLOB_RECURSE QUEBRATSK_HEADERS 
    "src/*.h"
    "src/*.hpp"
)

# Header search paths
include_directories(
    src/
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build dynamic library
add_library(quebratsk_engine SHARED ${QUEBRATSK_SOURCES} ${QUEBRATSK_HEADERS})

set_target_properties(quebratsk_engine PROPERTIES
    OUTPUT_NAME "libquebratsk"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/demo/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/demo/bin"
)
