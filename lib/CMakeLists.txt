# lib/CMakeLists.txt
# Imports all external libraries used in the project

include(FetchContent)

# ============================================================================
# Wasmtime imported library
# ============================================================================
add_library(wasmtime_lib SHARED IMPORTED GLOBAL)
set(WASMTIME_ROOT ${wasmtime_SOURCE_DIR})
if(WIN32)
    set_target_properties(wasmtime_lib PROPERTIES
        IMPORTED_LOCATION ${WASMTIME_ROOT}/lib/wasmtime.dll
        IMPORTED_IMPLIB ${WASMTIME_ROOT}/lib/wasmtime.dll.lib
        INTERFACE_INCLUDE_DIRECTORIES ${WASMTIME_ROOT}/include
    )
else()
    set_target_properties(wasmtime_lib PROPERTIES
        IMPORTED_LOCATION ${WASMTIME_ROOT}/lib/libwasmtime.so
        INTERFACE_INCLUDE_DIRECTORIES ${WASMTIME_ROOT}/include
    )
    set_target_properties(wasmtime_lib PROPERTIES
        IMPORTED_NO_SONAME TRUE
    )
endif()

# ============================================================================
# Slang imported library - for runtime shader compilation
# ============================================================================
add_library(slang_lib SHARED IMPORTED GLOBAL)
set(SLANG_ROOT ${slang_SOURCE_DIR})
if(WIN32)
    set_target_properties(slang_lib PROPERTIES
        IMPORTED_LOCATION ${SLANG_ROOT}/bin/slang.dll
        IMPORTED_IMPLIB ${SLANG_ROOT}/lib/slang.lib
        INTERFACE_INCLUDE_DIRECTORIES ${SLANG_ROOT}/include
    )
else()
    set_target_properties(slang_lib PROPERTIES
        IMPORTED_LOCATION ${SLANG_ROOT}/lib/libslang.so
        INTERFACE_INCLUDE_DIRECTORIES ${SLANG_ROOT}/include
    )
    set_target_properties(slang_lib PROPERTIES
        IMPORTED_NO_SONAME TRUE
    )
endif()

# ============================================================================
# STB Implementation Library - single source of STB functions
# ============================================================================
add_library(stb_impl SHARED ${CMAKE_CURRENT_SOURCE_DIR}/stb_impl.cpp)
target_include_directories(stb_impl PUBLIC ${stb_SOURCE_DIR})
set_target_properties(stb_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# ============================================================================
# VMA Implementation Library - single source of VMA functions
# Provides VMA_IMPLEMENTATION in one shared library
# ============================================================================
add_library(vma_impl SHARED ${CMAKE_CURRENT_SOURCE_DIR}/vma_impl.cpp)

# Use fetched Vulkan headers (must come BEFORE any system includes to take precedence)
target_include_directories(vma_impl PUBLIC ${vulkan_headers_SOURCE_DIR}/include)

# VMA header-only library from FetchContent
target_include_directories(vma_impl PUBLIC ${vma_SOURCE_DIR}/include)

# Vulkan-Hpp for dynamic dispatch
target_include_directories(vma_impl PUBLIC ${vulkan_hpp_SOURCE_DIR})

# Configure VMA to use vulkan.hpp dynamic dispatch
target_compile_definitions(vma_impl PRIVATE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

set_target_properties(vma_impl PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Link against dl for dlopen/dlsym on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(vma_impl PRIVATE ${CMAKE_DL_LIBS})
endif()

# ============================================================================
# Vulkan HPP Dispatch Library - provides VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
# ============================================================================
add_library(vulkan_hpp_dispatch SHARED ${CMAKE_CURRENT_SOURCE_DIR}/vulkan_hpp_dispatch.cpp)

# Use fetched Vulkan headers (must come BEFORE any system includes to take precedence)
target_include_directories(vulkan_hpp_dispatch PUBLIC ${vulkan_headers_SOURCE_DIR}/include)

# Vulkan-Hpp headers
target_include_directories(vulkan_hpp_dispatch PUBLIC ${vulkan_hpp_SOURCE_DIR})

# Configure for dynamic dispatch
target_compile_definitions(vulkan_hpp_dispatch PUBLIC
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)
target_compile_definitions(vulkan_hpp_dispatch PRIVATE
    VULKAN_HPP_STORAGE_SHARED
    VULKAN_HPP_STORAGE_SHARED_EXPORT
)

set_target_properties(vulkan_hpp_dispatch PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

# Link against dl for dlopen/dlsym on Linux
if(UNIX AND NOT APPLE)
    target_link_libraries(vulkan_hpp_dispatch PRIVATE ${CMAKE_DL_LIBS})
endif()

# ============================================================================
# Create main interface library for all external dependencies
# ============================================================================
add_library(external_libs INTERFACE)

# Thread pool (header-only)
target_include_directories(external_libs INTERFACE ${thread_pool_SOURCE_DIR}/include)

# Fetched Vulkan headers (must come BEFORE vulkan_hpp to take precedence)
target_include_directories(external_libs INTERFACE ${vulkan_headers_SOURCE_DIR}/include)

# Vulkan-Hpp headers
target_include_directories(external_libs INTERFACE ${vulkan_hpp_SOURCE_DIR})

# STB headers
target_include_directories(external_libs INTERFACE ${stb_SOURCE_DIR})

# Configure for dynamic dispatch across the project
target_compile_definitions(external_libs INTERFACE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

# ============================================================================
# Link libraries
# ============================================================================
target_link_libraries(external_libs INTERFACE
    glfw
    glm::glm
    GPUOpen::VulkanMemoryAllocator
    wasmtime_lib
    vulkan_hpp_dispatch
)

# ============================================================================
# Export variables for subprojects
# ============================================================================
set(EXTERNAL_LIBS external_libs PARENT_SCOPE)
set(WASMTIME_ROOT ${WASMTIME_ROOT} PARENT_SCOPE)
set(STB_INCLUDE_DIR ${stb_SOURCE_DIR} PARENT_SCOPE)
set(THREAD_POOL_INCLUDE_DIR ${thread_pool_SOURCE_DIR}/include PARENT_SCOPE)
set(VULKAN_HPP_INCLUDE_DIR ${vulkan_hpp_SOURCE_DIR} PARENT_SCOPE)
set(VULKAN_HEADERS_INCLUDE_DIR ${vulkan_headers_SOURCE_DIR}/include PARENT_SCOPE)
set(SLANG_ROOT ${SLANG_ROOT} PARENT_SCOPE)
set(VMA_INCLUDE_DIR ${vma_SOURCE_DIR}/include PARENT_SCOPE)
