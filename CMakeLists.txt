cmake_minimum_required(VERSION 3.15)
project(forge VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable C++ module scanning globally
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "")

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -O3)
endif()

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

# md4c (compile as C library)
add_library(md4c STATIC
    deps/md4c/src/md4c.c
    deps/md4c/src/md4c-html.c
    deps/md4c/src/entity.c
)
target_include_directories(md4c PUBLIC deps/md4c/src)

# QuickJS (compile as C library)
add_library(quickjs STATIC
    deps/quickjs/quickjs.c
    deps/quickjs/libregexp.c
    deps/quickjs/libunicode.c
    deps/quickjs/cutils.c
    deps/quickjs/dtoa.c # This contains js_dtoa, js_atod, etc.
    deps/quickjs/quickjs-libc.c # This contains additional utility functions
)
target_include_directories(quickjs PUBLIC deps/quickjs)
target_compile_definitions(quickjs PRIVATE
    CONFIG_VERSION="2024-01-13"
    _GNU_SOURCE
)
# QuickJS needs to be compiled as C code
set_target_properties(quickjs PROPERTIES
    C_STANDARD 11
    LINKER_LANGUAGE C
)

# yaml-cpp
add_subdirectory(deps/yaml-cpp)

# efsw
add_subdirectory(deps/efsw)

# Boost (Beast for WebSockets)
find_package(Boost REQUIRED COMPONENTS system)

# Embedding the livereload/script.min.js in binary so can be used in runtime
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/livereload.js.h
    COMMAND xxd -i assets/livereload/script.min.js > ${CMAKE_CURRENT_BINARY_DIR}/livereload.js.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/livereload/script.min.js
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Minifiers Bundle
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/minifiers.h
    COMMAND xxd -i assets/minifiers/minifiers_bundle.js > ${CMAKE_CURRENT_BINARY_DIR}/minifiers.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/minifiers/minifiers_bundle.js
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

# Forge executable
add_executable(forge
    src/main.cpp
    src/core/markdown.cpp
    src/core/frontmatter.cpp
    src/core/site_builder.cpp
    src/core/js_minifier.cpp
    src/core/html_minifier.cpp
    src/server/dev_server.cpp
    src/server/preview_server.cpp
    src/utils/file_watcher_listener.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/livereload.js.h
    ${CMAKE_CURRENT_BINARY_DIR}/minifiers.h
)

target_include_directories(forge PRIVATE
    src
    deps/md4c/src
    deps/quickjs
    deps/yaml-cpp/include
    deps/efsw/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(forge
    md4c
    quickjs
    yaml-cpp
    efsw
    pthread
    Boost::system
    dl # Required by QuickJS for dynamic loading
    m # Math library required by QuickJS
)

# Use -O0 for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g)
endif()

# Installation
install(TARGETS forge DESTINATION bin)
