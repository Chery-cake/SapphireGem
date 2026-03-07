# ==============================================================================
# Device Module 
# ==============================================================================

cmake_minimum_required(VERSION 3.25)

project(device
    VERSION 0.1.0
    DESCRIPTION "Device module - Vulkan instance, logical devices management, VMA memory, and shader management"
    LANGUAGES CXX C
)

# ==============================================================================
# Source Files
# ==============================================================================
file(GLOB_RECURSE DEVICE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB_RECURSE DEVICE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# ==============================================================================
# Library Target (Shared)
# ==============================================================================
add_library(${PROJECT_NAME} SHARED ${DEVICE_SOURCES} ${DEVICE_HEADERS})

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
        vulkan
        vma
        slang
        core
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
        DEVICE_EXPORTS
        $<$<CONFIG:Debug>:ENGINE_DEBUG>
)

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

message(STATUS "Configured: ${PROJECT_NAME} v${PROJECT_VERSION}")
