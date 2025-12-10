#pragma once

#include "buffer_manager.h"
#include "descriptor_pool_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <sys/types.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

// Handle different Vulkan SDK versions for DynamicLoader location
// Older SDKs (< 1.3.275) have it in vk::detail, newer in vk::
#ifndef VULKAN_HPP_NAMESPACE_STRING
  // Very old SDK
  using VulkanDynamicLoader = vk::detail::DynamicLoader;
#elif VK_HEADER_VERSION >= 275
  // Newer SDK (1.3.275+)
  using VulkanDynamicLoader = vk::DynamicLoader;
#else
  // Older SDK
  using VulkanDynamicLoader = vk::detail::DynamicLoader;
#endif

class Renderer {

private:
  GLFWwindow *window;
  VulkanDynamicLoader dl;

  vk::raii::Context context;
  vk::raii::Instance instance;
  vk::raii::SurfaceKHR surface;

  vk::raii::DebugUtilsMessengerEXT debugMessanger;

  std::unique_ptr<DeviceManager> deviceManager;
  std::unique_ptr<MaterialManager> materialManager;
  std::unique_ptr<DescriptorPoolManager> descriptorPoolManager;

  std::unique_ptr<BufferManager> bufferManager;

  void init_instance();
  void init_surface();
  void init_device();
  void init_swap_chain();
  void init_descriptor_pools();
  void init_materials();

  void init_debug();

  void create_buffers();

public:
  Renderer(GLFWwindow *window);
  ~Renderer();

  void reload();

  DeviceManager &get_device_manager();
};
