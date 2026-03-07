# ==============================================================================
# Graphics Dependencies
# ==============================================================================
# - Vulkan - Explicit GPU API
# - VMA - GPU memory manager
# - SDL3 - Window/input/events
# - SDL_image - Image loading
# - SDL_ttf - TrueType fonts loading
# - shader-slang/slang - Runtime shader compilation
# ==============================================================================

include(FetchContent)

# ==============================================================================
# Vulkan SDK (headers only)
# ==============================================================================
#FetchContent_Declare(
#    vulkan_headers
#    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
#    GIT_TAG vulkan-sdk-1.4.335.0
#    GIT_SHALLOW TRUE
#)
FetchContent_Declare(
    vulkan_hpp
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Hpp.git
    GIT_TAG v1.4.337
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vulkan_hpp)

add_library(vulkan SHARED vulkan.cpp)
target_include_directories(vulkan PUBLIC
  ${vulkan_hpp_SOURCE_DIR}/Vulkan-Headers/include
  ${vulkan_hpp_SOURCE_DIR}/vulkan
)
target_compile_definitions(vulkan PUBLIC
  VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)
#target_compile_definitions(vulkan PRIVATE
#  VULKAN_HPP_STORAGE_SHARED
#  VULKAN_HPP_STORAGE_SHARED_EXPORT
#)
set_target_properties(vulkan PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    C_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

if(WIN32)
  target_compile_definitions(vulkan PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
  target_compile_definitions(vulkan PUBLIC VK_USE_PLATFORM_METAL_EXT)
elseif(UNIX)
  target_compile_definitions(vulkan PUBLIC VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_WAYLAND_KHR)
endif()

# ==============================================================================
# VMA - Vulkan Memory Allocator
# ==============================================================================
#FetchContent_Declare(
#    _vma
#    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
#    GIT_TAG v3.3.0
#    GIT_SHALLOW TRUE
#)
FetchContent_Declare(
    _vma_hpp
    GIT_REPOSITORY https://github.com/YaaZ/VulkanMemoryAllocator-Hpp.git
    GIT_TAG v3.3.0+2
    GIT_SHALLOW TRUE
)
set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(_vma_hpp)

add_library(vma SHARED vma.cpp)
target_include_directories(vma PUBLIC
  ${_vma_hpp_SOURCE_DIR}/VulkanMemoryAllocator/include
  ${_vma_hpp_SOURCE_DIR}/include
)
target_link_libraries(vma PUBLIC
  vulkan
)
target_compile_definitions(vma PRIVATE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
)
set_target_properties(vma PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# ==============================================================================
# MoltenVK - Vulkan over Metal (macOS only)
# Fetches prebuilt binaries from GitHub releases — no build step needed.
# Creates a `moltenvk` INTERFACE target that the `vulkan` target links against.
# Also generates the ICD manifest so vk::detail::DynamicLoader finds it
# automatically without any hardcoded paths in C++ code.
# ==============================================================================
if(APPLE)
    FetchContent_Declare(
        _moltenvk
        URL https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.1/MoltenVK-macos.tar
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(_moltenvk)

    set(MOLTENVK_DYLIB
        "${_moltenvk_SOURCE_DIR}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
    )
    set(MOLTENVK_INCLUDE
        "${_moltenvk_SOURCE_DIR}/MoltenVK/include"
    )

    # Interface target — same pattern as your slang/vma targets
    add_library(moltenvk INTERFACE)
    target_include_directories(moltenvk INTERFACE "${MOLTENVK_INCLUDE}")
    target_link_libraries(moltenvk INTERFACE "${MOLTENVK_DYLIB}")

    # Generate the ICD manifest pointing at the dylib
    # OUTPUT_DIR is already defined in your root CMakeLists.txt
    set(MOLTENVK_ICD_DIR "${OUTPUT_DIR}/vulkan/icd.d")
    file(MAKE_DIRECTORY "${MOLTENVK_ICD_DIR}")
    file(GENERATE
        OUTPUT  "${MOLTENVK_ICD_DIR}/MoltenVK_icd.json"
        CONTENT "{\n    \"file_format_version\": \"1.0.0\",\n    \"ICD\": {\n        \"library_path\": \"../../libMoltenVK.dylib\",\n        \"api_version\": \"1.3.0\"\n    }\n}\n"
    )

    # Copy the dylib itself to OUTPUT_DIR so the ICD relative path resolves
    add_custom_target(copy_moltenvk ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MOLTENVK_DYLIB}"
            "${OUTPUT_DIR}/libMoltenVK.dylib"
        COMMENT "[MoltenVK] Copying libMoltenVK.dylib to output"
    )

    target_link_libraries(vulkan PUBLIC moltenvk)
    add_dependencies(vulkan copy_moltenvk)
    message(STATUS "MoltenVK configured (macOS)")
endif()

# ==============================================================================
# SDL3 - Window/Input/Events - doesn't have module'
# ==============================================================================
FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.0
    GIT_SHALLOW TRUE
)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl3)

# ==============================================================================
# SDL_image - Image Loading - doesn't have module
# ==============================================================================
FetchContent_Declare(
    sdl_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-3.2.6
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE FALSE
)
set(SDLIMAGE_SHARED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_STATIC OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TEST OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_TOOLS OFF CACHE BOOL "" FORCE)
# Control image format support (enable or disable as needed)
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BMP ON CACHE BOOL "" FORCE)
set(SDLIMAGE_GIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JPG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_LBM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_PNG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_PPM ON CACHE BOOL "" FORCE)
set(SDLIMAGE_QOI OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SVG ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TGA ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TIF ON CACHE BOOL "" FORCE)
set(SDLIMAGE_WEBP OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_XPM OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_EXIF OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl_image)

# ==============================================================================
# SDL_image - Image Loading - doesn't have module
# ==============================================================================
FetchContent_Declare(
    sdl_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-3.2.2
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE FALSE
)
set(SDLTTF_SHARED ON CACHE BOOL "" FORCE)
set(SDLTTF_STATIC OFF CACHE BOOL "" FORCE)
set(SDLTTF_TEST OFF CACHE BOOL "" FORCE)
set(SDLTTF_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sdl_ttf)

# ==============================================================================
# shader-slang/slang - Runtime shader compilation
# ==============================================================================
# Uses prebuilt binaries from GitHub releases to avoid building from source,
# which takes 30+ minutes on CI runners.
# ==============================================================================
set(SLANG_VERSION "2025.24.3")
set(SLANG_TAG "v${SLANG_VERSION}")

# Determine platform identifier for the prebuilt binary URL
if(WIN32)
    set(_SLANG_OS "windows")
elseif(APPLE)
    set(_SLANG_OS "macos")
else()
    set(_SLANG_OS "linux")
endif()

# Determine architecture
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    set(_SLANG_ARCH "aarch64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_SLANG_ARCH "x86_64")
else()
    message(FATAL_ERROR "Unsupported architecture for slang prebuilt: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(_SLANG_ARCHIVE "slang-${SLANG_VERSION}-${_SLANG_OS}-${_SLANG_ARCH}")
if(WIN32)
    set(_SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_TAG}/${_SLANG_ARCHIVE}.zip")
else()
    set(_SLANG_URL "https://github.com/shader-slang/slang/releases/download/${SLANG_TAG}/${_SLANG_ARCHIVE}.tar.gz")
endif()

FetchContent_Declare(
    _slang_prebuilt
    URL ${_SLANG_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(_slang_prebuilt)

add_library(slang INTERFACE)
target_include_directories(slang INTERFACE
    "${_slang_prebuilt_SOURCE_DIR}/include"
)
if(WIN32)
    target_link_libraries(slang INTERFACE
        "${_slang_prebuilt_SOURCE_DIR}/lib/slang.lib"
    )
else()
    target_link_libraries(slang INTERFACE
        "${_slang_prebuilt_SOURCE_DIR}/lib/libslang${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
endif()

# Copy slang shared libraries to output directory for runtime discovery
if(WIN32)
    set(_SLANG_RUNTIME_LIBS
        "${_slang_prebuilt_SOURCE_DIR}/bin/slang.dll"
        "${_slang_prebuilt_SOURCE_DIR}/bin/slang-compiler.dll"
        "${_slang_prebuilt_SOURCE_DIR}/bin/slang-rt.dll"
    )
    add_custom_target(copy_slang ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_SLANG_RUNTIME_LIBS} "${OUTPUT_DIR}"
        COMMENT "[slang] Copying DLLs to output"
    )
else()
    set(_SLANG_RUNTIME_LIBS
        "${_slang_prebuilt_SOURCE_DIR}/lib/libslang${CMAKE_SHARED_LIBRARY_SUFFIX}"
        "${_slang_prebuilt_SOURCE_DIR}/lib/libslang-rt${CMAKE_SHARED_LIBRARY_SUFFIX}"
    )
    add_custom_target(copy_slang ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_SLANG_RUNTIME_LIBS} "${OUTPUT_DIR}/lib"
        COMMENT "[slang] Copying shared libraries to output"
    )
endif()
add_dependencies(slang copy_slang)

message(STATUS "Graphics dependencies configured")
