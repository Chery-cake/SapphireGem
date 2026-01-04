cmake_minimum_required(VERSION 3.25)
project(SapphireEngine VERSION 1.0.0 LANGUAGES CXX C)

# C++ and C standard settings
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile commands for clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build shared libraries by default
set(BUILD_SHARED_LIBS ON)

# Set RPATH settings for finding shared libraries at runtime
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING
        "Choose the build type: Debug or Release"
        FORCE)
endif()

# Output directories based on OS
if(WIN32)
    set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/windows)
elseif(APPLE)
    set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/macos)
else()
    set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/linux)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR}/lib)

# Include cmake helper scripts
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

# Include clangd configuration generator
include(cmake/GenerateClangd.cmake)

# Include library fetching
include(cmake/FetchLibraries.cmake)

# Include definitions for Release and Debug
include(cmake/DefinitionsForVersion.cmake)

# Add library directory
add_subdirectory(lib)

# Add subprojects
add_subdirectory(subprojects)

# Copy assets to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    ${OUTPUT_DIR}/assets
    COMMENT "Copying assets to output directory"
)

# Copy wasmtime library to output lib directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${wasmtime_SOURCE_DIR}/lib/libwasmtime.so
    ${OUTPUT_DIR}/lib/
    COMMENT "Copying wasmtime library to output directory"
)

# Copy slang library to output lib directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${slang_SOURCE_DIR}/lib/libslang.so
    ${OUTPUT_DIR}/lib/
    COMMENT "Copying slang library to output directory"
)

# Generate .clangd file
generate_clangd_file()

# Cross-compilation support
if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross-compiling for: ${CMAKE_SYSTEM_NAME}")
endif()
