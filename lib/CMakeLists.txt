
add_subdirectory(steamworks)

# Download and setup Wasmtime runtime for WASM execution
set(WASMTIME_VERSION "28.0.0")
set(WASMTIME_DIR ${CMAKE_BINARY_DIR}/wasmtime)

if(WIN32)
    set(WASMTIME_PLATFORM "x86_64-windows")
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.zip")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/wasmtime.dll.lib")
    set(WASMTIME_DLL "${WASMTIME_DIR}/bin/wasmtime.dll")
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(WASMTIME_PLATFORM "aarch64-macos")
    else()
        set(WASMTIME_PLATFORM "x86_64-macos")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/libwasmtime.dylib")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(WASMTIME_PLATFORM "aarch64-linux")
    else()
        set(WASMTIME_PLATFORM "x86_64-linux")
    endif()
    set(WASMTIME_ARCHIVE "wasmtime-v${WASMTIME_VERSION}-${WASMTIME_PLATFORM}-c-api.tar.xz")
    set(WASMTIME_LIBRARY "${WASMTIME_DIR}/lib/libwasmtime.so")
    set(WASMTIME_DLL "${WASMTIME_LIBRARY}")
endif()

set(WASMTIME_URL "https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${WASMTIME_ARCHIVE}")
set(WASMTIME_INCLUDE_DIR "${WASMTIME_DIR}/include")

# Download and extract Wasmtime if not already present
if(NOT EXISTS ${WASMTIME_LIBRARY})
    message(STATUS "Downloading Wasmtime ${WASMTIME_VERSION} for ${WASMTIME_PLATFORM}...")
    file(DOWNLOAD ${WASMTIME_URL} ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        SHOW_PROGRESS
        STATUS WASMTIME_DOWNLOAD_STATUS)
    
    list(GET WASMTIME_DOWNLOAD_STATUS 0 WASMTIME_ERROR)
    if(WASMTIME_ERROR)
        message(FATAL_ERROR "Failed to download Wasmtime from ${WASMTIME_URL}")
    endif()
    
    message(STATUS "Extracting Wasmtime to ${WASMTIME_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${WASMTIME_ARCHIVE}
        DESTINATION ${WASMTIME_DIR})
endif()

message(STATUS "Wasmtime library: ${WASMTIME_LIBRARY}")
message(STATUS "Wasmtime include dir: ${WASMTIME_INCLUDE_DIR}")

# Create Wasmtime interface library target
add_library(wasmtime INTERFACE)
target_include_directories(wasmtime INTERFACE ${WASMTIME_INCLUDE_DIR})
target_link_libraries(wasmtime INTERFACE ${WASMTIME_LIBRARY})

# Copy Wasmtime shared library to output directory
add_custom_target(copy_wasmtime_library ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WASMTIME_DLL}
        ${COMMON_OUTPUT_DIR}
    COMMENT "Copying Wasmtime library to output directory"
    DEPENDS ${WASMTIME_DLL}
)

# Download and setup Slang WASM for platform-independent shader compilation
set(SLANG_FALLBACK_VERSION "2025.24.2")
set(SLANG_VERSION ${SLANG_FALLBACK_VERSION})

# Try to fetch the latest release information from GitHub API
message(STATUS "Attempting to fetch latest Slang release...")
file(DOWNLOAD
    "https://api.github.com/repos/shader-slang/slang/releases/latest"
    "${CMAKE_BINARY_DIR}/slang_latest_release.json"
    TIMEOUT 10
    STATUS API_DOWNLOAD_STATUS
    LOG API_DOWNLOAD_LOG
)

list(GET API_DOWNLOAD_STATUS 0 API_ERROR_CODE)
if(API_ERROR_CODE EQUAL 0 AND EXISTS "${CMAKE_BINARY_DIR}/slang_latest_release.json")
    file(READ "${CMAKE_BINARY_DIR}/slang_latest_release.json" SLANG_RELEASE_JSON)
    # Try to parse the tag_name from JSON
    string(JSON SLANG_TAG_NAME ERROR_VARIABLE JSON_ERROR GET ${SLANG_RELEASE_JSON} tag_name)
    if(NOT JSON_ERROR AND SLANG_TAG_NAME)
        # Remove 'v' prefix if present
        string(REPLACE "v" "" SLANG_VERSION ${SLANG_TAG_NAME})
        message(STATUS "Found latest Slang release: ${SLANG_VERSION}")
    else()
        message(STATUS "Could not parse release info, using fallback version ${SLANG_FALLBACK_VERSION}")
    endif()
else()
    message(STATUS "Could not fetch latest release info (API may be blocked), using fallback version ${SLANG_FALLBACK_VERSION}")
endif()

# Download Slang WASM package
set(SLANG_WASM_DIR ${CMAKE_BINARY_DIR}/slang-wasm)
set(SLANG_WASM_ARCHIVE "slang-${SLANG_VERSION}-wasm.zip")
set(SLANG_WASM_URL "https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/${SLANG_WASM_ARCHIVE}")

if(NOT EXISTS ${SLANG_WASM_DIR}/slang-wasm.js)
    message(STATUS "Downloading Slang WASM ${SLANG_VERSION}...")
    message(STATUS "URL: ${SLANG_WASM_URL}")
    file(DOWNLOAD ${SLANG_WASM_URL} ${CMAKE_BINARY_DIR}/${SLANG_WASM_ARCHIVE}
        SHOW_PROGRESS
        STATUS SLANG_WASM_DOWNLOAD_STATUS)
    
    list(GET SLANG_WASM_DOWNLOAD_STATUS 0 SLANG_WASM_ERROR)
    if(SLANG_WASM_ERROR)
        message(FATAL_ERROR "Failed to download Slang WASM from ${SLANG_WASM_URL}")
    endif()
    
    message(STATUS "Extracting Slang WASM to ${SLANG_WASM_DIR}")
    file(ARCHIVE_EXTRACT
        INPUT ${CMAKE_BINARY_DIR}/${SLANG_WASM_ARCHIVE}
        DESTINATION ${SLANG_WASM_DIR})
endif()

message(STATUS "Slang WASM directory: ${SLANG_WASM_DIR}")

# Copy Slang WASM files to output directory for runtime access
add_custom_target(copy_slang_wasm ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${SLANG_WASM_DIR}
        ${COMMON_OUTPUT_DIR}/slang-wasm
    COMMENT "Copying Slang WASM files to output directory"
    DEPENDS ${SLANG_WASM_DIR}/slang-wasm.js
)

find_package(glfw3 REQUIRED)

target_compile_definitions(glfw INTERFACE
    GLFW_INCLUDE_VULKAN
)

if(NOT TARGET glm)
  add_library(glm INTERFACE)
  target_include_directories(glm INTERFACE /usr/include)
endif()

find_package(Vulkan REQUIRED)
add_library(vulkan INTERFACE)

target_compile_definitions(vulkan INTERFACE
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(vulkan INTERFACE
    "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(vulkan INTERFACE
    Vulkan::Vulkan
)

# Only fetch if not already available
if(NOT TARGET VulkanMemoryAllocator)
    include(FetchContent)

    FetchContent_Declare(
        VulkanMemoryAllocator
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(VulkanMemoryAllocator)

    FetchContent_GetProperties(VulkanMemoryAllocator)
    message(STATUS "VMA source: ${vulkanmemoryallocator_SOURCE_DIR}")
    message(STATUS "VMA binary: ${vulkanmemoryallocator_BINARY_DIR}")
endif()

add_library(vma INTERFACE)

target_include_directories(vma INTERFACE
    ${vulkanmemoryallocator_SOURCE_DIR}/src
    ${vulkanmemoryallocator_SOURCE_DIR}/include
)

target_compile_definitions(vma INTERFACE
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

target_link_libraries(vma INTERFACE Vulkan::Vulkan)

find_package(Threads REQUIRED)

# Fetch BS::thread_pool
if(NOT TARGET BS::thread_pool)
    include(FetchContent)

    FetchContent_Declare(
        bs_thread_pool
        GIT_REPOSITORY https://github.com/bshoshany/thread-pool.git
        GIT_TAG v5.0.0  # Use latest stable version
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(bs_thread_pool)

    FetchContent_GetProperties(bs_thread_pool)
    message(STATUS "BTP source: ${bs_thread_pool_SOURCE_DIR}")
    message(STATUS "BTP binary: ${bs_thread_pool_BINARY_DIR}")
endif()

add_library(btp INTERFACE)

target_compile_definitions(btp INTERFACE
    # BS_THREAD_POOL_NATIVE_EXTENSIONS
)

target_include_directories(btp INTERFACE
    ${bs_thread_pool_SOURCE_DIR}/include
    ${bs_thread_pool_SOURCE_DIR}/modules
)
target_link_libraries(btp INTERFACE
    Threads::Threads
)

# Fetch stb (stb_image for image loading)
if(NOT TARGET stb)
    include(FetchContent)

    FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
    )

    FetchContent_MakeAvailable(stb)

    FetchContent_GetProperties(stb)
    message(STATUS "STB source: ${stb_SOURCE_DIR}")
endif()

add_library(stb INTERFACE)

target_include_directories(stb INTERFACE
    ${stb_SOURCE_DIR}
)
