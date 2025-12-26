set(CMAKE_C_COMPILER "/usr/bin/clang" CACHE PATH "C compiler" FORCE)
set(CMAKE_CXX_COMPILER "/usr/bin/clang++" CACHE PATH "C++ compiler" FORCE)

cmake_minimum_required(VERSION 3.30)

project(Exemple LANGUAGES C CXX)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "Choose the build type: Debug or Release"
        FORCE)
endif()

option(TEST_MEMORY_LEAKS "Enable ASan to test for memory leaks" OFF)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# Set common output directory for all targets
set(COMMON_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin)
set(COMMON_OUTPUT_DIR ${COMMON_OUTPUT_DIR} CACHE INTERNAL "Common output directory for all targets")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${COMMON_OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${COMMON_OUTPUT_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${COMMON_OUTPUT_DIR})

# Also set per-configuration output directories to ensure consistency
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} ${COMMON_OUTPUT_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} ${COMMON_OUTPUT_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} ${COMMON_OUTPUT_DIR})
endforeach()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Output directory: ${COMMON_OUTPUT_DIR}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Optimizations
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)  # LTO

    # Strip symbols where applicable
    if(CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
        add_link_options(-s)  # GCC/Clang: strip symbols
    endif()

    add_compile_definitions(
        NDEBUG  # Disable assertions
    )
else()
    # Compiler flags
    add_compile_definitions(DEBUG_ENABLED)  # Custom define for debug code
    add_compile_options(
        "$<$<C_COMPILER_ID:MSVC>:/Zi>"     # MSVC debug symbols
        "$<$<CXX_COMPILER_ID:MSVC>:/Zi>"
        "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-g3>"  # GCC/Clang debug symbols
        "$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-g3>"
    )

    # Linker flags
    add_link_options(
        "$<$<C_COMPILER_ID:MSVC>:/DEBUG>"  # MSVC PDB generation
    )

    # Disable optimizations
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")

    # Enable assertions (remove in production)
    add_compile_definitions(ENABLE_ASSERTIONS)

    if(TEST_MEMORY_LEAKS)

        add_compile_options(
            -fsanitize=address
            -fsanitize=undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
        )
        add_link_options(
            -fsanitize=address
            -fsanitize=undefined
        )

        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            add_compile_options(-fno-var-tracking)
        endif()

    endif()

endif()

# Create the common output directory if it doesn't exist
file(MAKE_DIRECTORY ${COMMON_OUTPUT_DIR})

# Copy assets folder to output directory (including .slang source files for runtime compilation)
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${COMMON_OUTPUT_DIR}/assets
    COMMENT "Copying assets folder to output directory"
)

add_subdirectory(lib)
add_subdirectory(subprojects)

# generate .clangd file

set(projects
    steamworks
    vulkan
    vma
    btp
    stb
)

set(inc_dirs)
foreach(proj ${projects})
    get_target_property(dirs ${proj} INTERFACE_INCLUDE_DIRECTORIES)
    if(dirs)
        list(APPEND inc_dirs ${dirs})
    endif()
endforeach()

set(clangd_includes)
foreach(dir ${inc_dirs})
    if(IS_ABSOLUTE "${dir}" AND EXISTS "${dir}")
        file(TO_NATIVE_PATH "${dir}" native_dir)
        string(REPLACE "\\" "\\\\" native_dir "${native_dir}")
        list(APPEND clangd_includes "- \"-I${native_dir}\"\n")
    endif()
endforeach()

list(REMOVE_DUPLICATES clangd_includes)
string(REPLACE ";" "        " clangd_includes "${clangd_includes}")

set(clangd_content "
CompileFlags:
    Add:
        ${clangd_includes}
        - \"-std=c${CMAKE_C_STANDARD}\"
        - \"-std=c++${CMAKE_CXX_STANDARD}\"
        - \"-std=gnu++${CMAKE_CXX_STANDARD}\"
        - \"-Wall\"
        - \"-Wextra\"
        - \"-Wpedantic\"
    CompilationDatabase: ${CMAKE_BINARY_DIR}

Diagnostics:
    ClangTidy:
        Add: ['bugprone-*', 'performance-*', 'readability-*', 'portability-*', 'concurrency-*', 'cppcoreguidelines-*']
    UnusedIncludes: Strict
    MissingIncludes: Strict

Index:
    Background: Build
")

file(WRITE "${CMAKE_SOURCE_DIR}/.clangd" "${clangd_content}")
message(STATUS "Generated .clangd configuration file")
