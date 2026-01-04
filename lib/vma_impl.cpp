// VMA Implementation - this file provides the VMA function implementations
// Must be in a separate shared library and linked by all modules that use VMA.
//
// Uses vulkan.hpp dynamic dispatch loader for Vulkan function loading.

// Include vulkan.hpp first with dynamic dispatch enabled
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

// Configure VMA to use dynamic function loading
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

// Provide VMA implementation
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
