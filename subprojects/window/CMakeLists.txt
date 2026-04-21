# ==============================================================================
# Window Engine Module
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

project(window
    VERSION 0.1.0
    DESCRIPTION "Window engine module - SDL windown management, swapchain, rendering"
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
        $<BUILD_INTERFACE: ${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ==============================================================================
# Dependencies
# ==============================================================================
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        device
        SDL3::SDL3
        SDL3_image::SDL3_image
        core
        glm::glm
)

# ==============================================================================
# Compiler Options
# ==============================================================================
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4251>
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

add_engine_module(${PROJECT_NAME})

# ==============================================================================
# Version and Library Properties
# ==============================================================================
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME ${PROJECT_NAME}
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# ==============================================================================
# Apply Build Configuration (Debug/Release)
# ==============================================================================
if(COMMAND target_configure_build)
    target_configure_build(${PROJECT_NAME}
        ENABLE_LTO
    )
endif()

if(COMMAND target_configure_platform)
    target_configure_platform(${PROJECT_NAME})
endif()

if(COMMAND target_configure_rpath)
    target_configure_rpath(${PROJECT_NAME})
endif()

message(STATUS "Configured:  ${PROJECT_NAME} v${PROJECT_VERSION}")
