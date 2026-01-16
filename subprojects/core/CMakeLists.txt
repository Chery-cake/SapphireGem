# ==============================================================================
# Core Engine Module
# ==============================================================================
# Provides common functionality shared across all engine modules:
# - Thread-pool management
# - Asset registry
# - Main engine configurations
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

project(core 
    VERSION 0.1.0
    DESCRIPTION "Core engine module - Threading, asset registry"
    LANGUAGES CXX C
)

# ==============================================================================
# Source Files
# ==============================================================================
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB_RECURSE CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# ==============================================================================
# Library Target (Shared)
# ==============================================================================
add_library(${PROJECT_NAME} SHARED ${CORE_SOURCES} ${CORE_HEADERS})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==============================================================================
# Dependencies
# ==============================================================================
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        nlohmann_json::nlohmann_json
        glm::glm
    PRIVATE
        thread_pool
)

# ==============================================================================
# Compiler Options (Per-Target Only)
# ==============================================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        ENGINE_CORE_EXPORTS
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

# ==============================================================================
# Version Information
# ==============================================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME ${PROJECT_NAME}
    INSTALL_RPATH "$ORIGIN"
    BUILD_RPATH "$ORIGIN"
)

message(STATUS "Configured: ${PROJECT_NAME} v${PROJECT_VERSION}")
