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

// Handle different Vulkan-HPP header versions for DynamicLoader location
// Starting with VK_HEADER_VERSION 301, DynamicLoader moved to vk::detail namespace
// See: https://github.com/KhronosGroup/Vulkan-Hpp/issues/1596
#if !defined(VK_HEADER_VERSION) || VK_HEADER_VERSION < 301
  // Older Vulkan-HPP (< 301): DynamicLoader is in vk namespace
  using VulkanDynamicLoader = vk::DynamicLoader;
#else
  // Newer Vulkan-HPP (>= 301): DynamicLoader is in vk::detail namespace
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
