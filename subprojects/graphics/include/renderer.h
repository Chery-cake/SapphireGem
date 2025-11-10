#pragma once

#include "devices_manager.h"
#include <memory>
#include <sys/types.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class Renderer {

private:
  GLFWwindow *window;
  vk::detail::DynamicLoader dl;

  vk::raii::Context context;
  vk::raii::Instance instance;
  vk::raii::SurfaceKHR surface;

  vk::raii::DebugUtilsMessengerEXT debugMessanger;

  std::unique_ptr<DevicesManager> devicesManager;

  void init_instance();
  void init_surface();
  void init_device();

  void init_debug();

public:
  Renderer(GLFWwindow *window);
  ~Renderer();
};
