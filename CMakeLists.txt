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

# Download and setup Wasmtime runtime for WASM execution
set(WASMTIME_VERSION "28.0.0")
set(WASMTIME_DIR ${CMAKE_BINARY_DIR}/wasmtime)

if(WIN32)
    set(WASMTIME_PLATFORM "x86_64-windows")
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.zip")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/wasmtime.dll.lib")
    set(WASMTIME_DLL "${WASMTIME_DIR}/bin/wasmtime.dll")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(WASMTIME_PLATFORM "aarch64-macos")
    else()
        set(WASMTIME_PLATFORM "x86_64-macos")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/libwasmtime.dylib")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(WASMTIME_PLATFORM "aarch64-linux")
    else()
        set(WASMTIME_PLATFORM "x86_64-linux")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/libwasmtime.so")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
endif()

set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${WASMTIME_ARCHIVE}")
set(WASMTIME_INCLUDE_DIR "${WASMTIME_DIR}/include")

# Download and extract Wasmtime if not already present
if(NOT EXISTS ${WASMTIME_LIBRARY})
    message(STATUS "Downloading Wasmtime ${WASMTIME_VERSION} for ${WASMTIME_PLATFORM}...")
    file(DOWNLOAD ${WASMTIME_URL} ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        SHOW_PROGRESS
        STATUS WASMTIME_DOWNLOAD_STATUS)
    
    list(GET WASMTIME_DOWNLOAD_STATUS 0 WASMTIME_ERROR)
    if(WASMTIME_ERROR)
        message(FATAL_ERROR "Failed to download Wasmtime from ${WASMTIME_URL}")
    endif()
    
    message(STATUS "Extracting Wasmtime to ${WASMTIME_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        DESTINATION ${WASMTIME_DIR})
endif()

message(STATUS "Wasmtime library: ${WASMTIME_LIBRARY}")
message(STATUS "Wasmtime include dir: ${WASMTIME_INCLUDE_DIR}")

# Create Wasmtime interface library target
add_library(wasmtime INTERFACE)
target_include_directories(wasmtime INTERFACE ${WASMTIME_INCLUDE_DIR})
target_link_libraries(wasmtime INTERFACE ${WASMTIME_LIBRARY})

# Copy Wasmtime shared library to output directory
add_custom_target(copy_wasmtime_library ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WASMTIME_DLL}
        ${COMMON_OUTPUT_DIR}
    COMMENT "Copying Wasmtime library to output directory"
    DEPENDS ${WASMTIME_DLL}
)

# Download and setup Slang WASM for platform-independent shader compilation
set(SLANG_FALLBACK_VERSION "2025.24.2")
set(SLANG_VERSION ${SLANG_FALLBACK_VERSION})

# Try to fetch the latest release information from GitHub API
message(STATUS "Attempting to fetch latest Slang release...")
file(DOWNLOAD
    "https://api.github.com/repos/shader-slang/slang/releases/latest"
    "${CMAKE_BINARY_DIR}/slang_latest_release.json"
    TIMEOUT 10
    STATUS API_DOWNLOAD_STATUS
    LOG API_DOWNLOAD_LOG
)

list(GET API_DOWNLOAD_STATUS 0 API_ERROR_CODE)
if(API_ERROR_CODE EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/slang_latest_release.json")
    file(READ "${CMAKE_BINARY_DIR}/slang_latest_release.json" SLANG_RELEASE_JSON)
    # Try to parse the tag_name from JSON
    string(JSON SLANG_TAG_NAME ERROR_VARIABLE JSON_ERROR GET ${SLANG_RELEASE_JSON} tag_name)
    if(NOT JSON_ERROR AND SLANG_TAG_NAME)
        # Remove 'v' prefix if present
        string(REPLACE "v" "" SLANG_VERSION ${SLANG_TAG_NAME})
        message(STATUS "Found latest Slang release: ${SLANG_VERSION}")
    else()
        message(STATUS "Could not parse release info, using fallback version ${SLANG_FALLBACK_VERSION}")
    endif()
else()
    message(STATUS "Could not fetch latest release info (API may be blocked), using fallback version ${SLANG_FALLBACK_VERSION}")
endif()

# Download Slang WASM package
set(SLANG_WASM_DIR ${CMAKE_BINARY_DIR}/slang-wasm)
set(SLANG_WASM_ARCHIVE "slang-${SLANG_VERSION}-wasm.zip")
set(SLANG_WASM_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_WASM_ARCHIVE}")

if(NOT EXISTS ${SLANG_WASM_DIR}/slang-wasm.js)
    message(STATUS "Downloading Slang WASM ${SLANG_VERSION}...")
    message(STATUS "URL: ${SLANG_WASM_URL}")
    file(DOWNLOAD ${SLANG_WASM_URL} ${CMAKE_BINARY_DIR}/${SLANG_WASM_ARCHIVE}
        SHOW_PROGRESS
        STATUS SLANG_WASM_DOWNLOAD_STATUS)
    
    list(GET SLANG_WASM_DOWNLOAD_STATUS 0 SLANG_WASM_ERROR)
    if(SLANG_WASM_ERROR)
        message(FATAL_ERROR "Failed to download Slang WASM from ${SLANG_WASM_URL}")
    endif()
    
    message(STATUS "Extracting Slang WASM to ${SLANG_WASM_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${SLANG_WASM_ARCHIVE}
        DESTINATION ${SLANG_WASM_DIR})
endif()

message(STATUS "Slang WASM directory: ${SLANG_WASM_DIR}")

# Copy Slang WASM files to output directory for runtime access
add_custom_target(copy_slang_wasm ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SLANG_WASM_DIR}
        ${COMMON_OUTPUT_DIR}/slang-wasm
    COMMENT "Copying Slang WASM files to output directory"
    DEPENDS ${SLANG_WASM_DIR}/slang-wasm.js
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
