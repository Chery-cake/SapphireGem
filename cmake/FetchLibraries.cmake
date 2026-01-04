# FetchLibraries.cmake
# Downloads and configures external dependencies using FetchContent
# Libraries target Vulkan 1.4 with minimum support for Vulkan 1.3

include(FetchContent)

# Set FetchContent to be quiet
set(FETCHCONTENT_QUIET OFF)

# ============================================================================
# Vulkan-Headers - Core Vulkan headers (must match vulkan_hpp version)
# ============================================================================
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.335
    GIT_SHALLOW TRUE
)

# ============================================================================
# Vulkan-Hpp - C++ bindings for Vulkan with dynamic dispatch loader
# Uses VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE for dynamic loading
# v1.4.335 targets Vulkan 1.4 SDK
# ============================================================================
FetchContent_Declare(
    vulkan_hpp
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
    GIT_TAG v1.4.335
    GIT_SHALLOW TRUE
)

# ============================================================================
# Slang - Shader compilation library for runtime shader compilation
# ============================================================================
set(SLANG_VERSION "2024.14.5")
if(WIN32)
    set(SLANG_PLATFORM "windows-x86_64")
    set(SLANG_EXT "zip")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(SLANG_PLATFORM "macos-aarch64")
    else()
        set(SLANG_PLATFORM "macos-x86_64")
    endif()
    set(SLANG_EXT "zip")
else()
    set(SLANG_PLATFORM "linux-x86_64")
    set(SLANG_EXT "zip")
endif()

FetchContent_Declare(
    slang
    URL https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-${SLANG_PLATFORM}.${SLANG_EXT}
)

# ============================================================================
# GLFW3
# ============================================================================
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# ============================================================================
# VMA (Vulkan Memory Allocator) - v3.1.0 supports Vulkan 1.3+
# ============================================================================
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)

# ============================================================================
# GLM
# ============================================================================
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)

# ============================================================================
# Thread Pool (bshoshany/thread-pool)
# ============================================================================
FetchContent_Declare(
    thread_pool
    GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
    GIT_TAG v4.1.0
    GIT_SHALLOW TRUE
)

# ============================================================================
# Wasmtime - download prebuilt binaries for performance
# ============================================================================
set(WASMTIME_VERSION "28.0.0")
if(WIN32)
    set(WASMTIME_PLATFORM "x86_64-windows-c-api")
    set(WASMTIME_EXT "zip")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(WASMTIME_PLATFORM "aarch64-macos-c-api")
    else()
        set(WASMTIME_PLATFORM "x86_64-macos-c-api")
    endif()
    set(WASMTIME_EXT "tar.xz")
else()
    set(WASMTIME_PLATFORM "x86_64-linux-c-api")
    set(WASMTIME_EXT "tar.xz")
endif()

FetchContent_Declare(
    wasmtime
    URL https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}.${WASMTIME_EXT}
)

# ============================================================================
# STB for image loading
# ============================================================================
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)

# ============================================================================
# Fetch all libraries
# ============================================================================
message(STATUS "Fetching external libraries...")
FetchContent_MakeAvailable(vulkan_headers vulkan_hpp slang glfw glm thread_pool stb vma wasmtime)
