// vulkan.cpp
// This file provides the storage for the Vulkan-Hpp dynamic dispatch loader.
// Must be compiled into a single shared library that all other modules link
// against.
//
// VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE provides the global
// vk::DispatchLoaderDynamic instance that all Vulkan-Hpp calls use.
// Uses vk::detail::DynamicLoader for loading Vulkan library and getting
// function pointers.

#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
