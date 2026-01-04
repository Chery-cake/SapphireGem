// vulkan_hpp_dispatch.cpp
// This file provides the storage for the Vulkan-Hpp dynamic dispatch loader.
// Must be compiled into a single shared library that all other modules link against.
//
// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE provides the global
// vk::DispatchLoaderDynamic instance that all Vulkan-Hpp calls use.
// Uses vk::detail::DynamicLoader for loading Vulkan library and getting function pointers.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

// Define the global dynamic dispatch loader storage
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
