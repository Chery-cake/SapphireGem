# ==============================================================================
# Modular C++ Engine - Root CMakeLists.txt
# ==============================================================================

cmake_minimum_required(VERSION 3.30)
set(CMAKE_POLICY_VERSION_MINIMUM 3.30)
enable_testing()

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
# Global Settings
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
  set(CMAKE_C_STANDARD 17)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
else()
  set(CMAKE_C_STANDARD 23)
  set(CMAKE_C_STANDARD_REQUIRED ON)
  set(CMAKE_C_EXTENSIONS OFF)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable POSIX features
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(_GNU_SOURCE)
else()
    add_compile_definitions(_POSIX_C_SOURCE=200809L)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

# Set output directories
set(TARGET_OS ${CMAKE_HOST_SYSTEM_NAME})
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
include(BuildConfiguration)
include(CrossCompiling)
include(HeadlessMode)

# ==============================================================================
# Dependencies
# ==============================================================================
add_subdirectory(lib)

# ==============================================================================
# Engine Subprojects
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
    device
    window
    #render
    #audio
    #physics
    #runtime
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# Add ENGINE_DEBUG definition for Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# ==============================================================================
# Apply Build Configuration
# ==============================================================================
target_configure_build(${PROJECT_NAME}
    ENABLE_LTO
    ENABLE_SANITIZERS
)

# Apply platform-specific configuration
target_configure_platform(${PROJECT_NAME})
target_configure_rpath(${PROJECT_NAME})

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
# Tests
# ==============================================================================
option(ENABLE_TESTS "Add and Compile the project tests" OFF)
if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ${OUTPUT_DIR}/assets DESTINATION share/engine)

# ==============================================================================
# Debug Output
# ==============================================================================
message(STATUS "")
message(STATUS "========================================")
message(STATUS " Modular C++ Engine v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS " Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS " C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS " C Standard: ${CMAKE_C_STANDARD}")
message(STATUS " Output: ${OUTPUT_DIR}")
message(STATUS " Runtime:  ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS " Library: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "========================================")

# Print build configuration summary
print_build_configuration()

# Print headless mode info
print_headless_mode_info()

# Print cross-compilation info if applicable
print_cross_compilation_info()
