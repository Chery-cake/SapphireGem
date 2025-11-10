
add_subdirectory(steamworks)

find_package(glfw3 REQUIRED)

find_package(glm CONFIG REQUIRED)

find_package(Vulkan REQUIRED)
add_library(vulkan)

target_compile_definitions(vulkan PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
        VULKAN_HPP_NO_STRUCT_CONSTRUCTORS=1
)
target_include_directories(vulkan
        PUBLIC
        "${Vulkan_INCLUDE_DIR}"
)
target_link_libraries(vulkan
        PUBLIC
        Vulkan::Vulkan
)

target_compile_features( vulkan PUBLIC cxx_std_20 )
set_target_properties(vulkan PROPERTIES CXX_STANDARD 20)

target_sources(vulkan
        PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS
        "${Vulkan_INCLUDE_DIR}"
        FILES
        "${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm"
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

target_include_directories(btp INTERFACE
    ${bs_thread_pool_SOURCE_DIR}/include
    ${bs_thread_pool_SOURCE_DIR}/modules
)

target_compile_definitions(btp INTERFACE
    # BS_THREAD_POOL_NATIVE_EXTENSIONS
)

target_link_libraries(btp INTERFACE Threads::Threads)
