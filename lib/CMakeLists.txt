
add_subdirectory(steamworks)

find_package(glfw3 REQUIRED)

target_compile_definitions(glfw INTERFACE
    GLFW_INCLUDE_VULKAN
)

if(NOT TARGET glm)
  add_library(glm INTERFACE)
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
