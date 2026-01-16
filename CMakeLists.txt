# ==============================================================================
# Modular C++ Engine - Root CMakeLists.txt
# ==============================================================================
# Orchestrates all subprojects, global dependency configuration, versioning,
# and build types. Enforces out-of-source builds and modern CMake practices.
# ==============================================================================

cmake_minimum_required(VERSION 3.30)
set(CMAKE_POLICY_VERSION_MINIMUM 3.30)

project(SapphireEngine
    VERSION 0.1.0
    DESCRIPTION "Modular C++ Engine with hot-reload, multi-thread and multi-gpu support"
    LANGUAGES CXX C
)

# ==============================================================================
# Enforce Out-of-Source Build
# ==============================================================================
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Please use out-of-source builds:\n"
                        "  cmake -B out -S .")
endif()

# ==============================================================================
# Global Settings (No Global Compiler Flags - Per-Target Only)
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Export compile_commands.json for tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable POSIX features
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
else()
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

# Default to Release build if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Set output directories
set(TARGET_OS ${CMAKE_HOST_SYSTEM_NAME}) # TODO add system to define OS for cross-compiling
set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}/${TARGET_OS})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

# Force color diagnostics
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_options(/color)
endif()

# ==============================================================================
# Custom CMake Modules
# ==============================================================================
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)

# ==============================================================================
# Dependencies (via lib/ subdirectory)
# ==============================================================================
add_subdirectory(lib)

# ==============================================================================
# Engine Subprojects (Shared Libraries via subprojects/ subdirectory)
# ==============================================================================
add_subdirectory(subprojects)

# ==============================================================================
# Main Application
# ==============================================================================
file(GLOB_RECURSE MAIN_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_executable(${PROJECT_NAME} ${MAIN_FILES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GENERATED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    core
    #window
    #device
    #render
    #audio
    #physics
    #runtime
)

# Per-target compiler options (never global)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Add ENGINE_DEBUG definition for Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
  INSTALL_RPATH "$ORIGIN/lib"
  BUILD_RPATH "$ORIGIN/lib"
  BUILD_WITH_INSTALL_RPATH ON
)

# ==============================================================================
# Copy Assets to Build Directory
# ==============================================================================
add_custom_target(copy_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets
        ${OUTPUT_DIR}/assets
    COMMENT "Copying assets to build directory"
)

add_dependencies(${PROJECT_NAME} copy_assets)

# ==============================================================================
# Generate .clangd Configuration
# ==============================================================================
include(GenerateClangd)

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${OUTPUT_DIR}/assets DESTINATION share/engine)

message(STATUS "")
message(STATUS "========================================")
message(STATUS " Modular C++ Engine v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS " Output: ${OUTPUT_DIR}")
message(STATUS "========================================")
