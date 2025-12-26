cmake_minimum_required(VERSION 3.30)

# Detect default target OS before project() is called
# Note: TARGET_OS uses lowercase names for consistency in output paths
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    set(DEFAULT_TARGET_OS "linux")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set(DEFAULT_TARGET_OS "windows")
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(DEFAULT_TARGET_OS "macos")
else()
    # For unsupported platforms, use the CMAKE_HOST_SYSTEM_NAME in lowercase
    string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" DEFAULT_TARGET_OS)
endif()

set(TARGET_OS "${DEFAULT_TARGET_OS}" CACHE STRING "Target operating system (linux, windows, macos)")
set_property(CACHE TARGET_OS PROPERTY STRINGS linux windows macos)

# Add cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Setup cross-compilation if needed (must be before project())
include(SetupCrossCompilation)

project(Exemple LANGUAGES C CXX)

message(STATUS "Target OS: ${TARGET_OS}")

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

# CMAKE_CXX_SCAN_FOR_MODULES: Only set if CMake version supports it (3.28+)
# Currently disabled for compatibility as C++20 module support is still experimental in most compilers
# Note: Project requires CMake 3.30, so this check is always true, but kept for clarity and future-proofing
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    message(STATUS "CMAKE_CXX_SCAN_FOR_MODULES: OFF (disabled for compatibility)")
else()
    message(STATUS "CMAKE_CXX_SCAN_FOR_MODULES: Not available (requires CMake 3.28+)")
endif()

# Set common output directory for all targets with OS-specific subdirectory
set(COMMON_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/bin/${TARGET_OS})
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

# Generate .clangd configuration file for IDE support
include(GenerateClangdConfig)

