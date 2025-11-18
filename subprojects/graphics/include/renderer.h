#pragma once

#include "devices_manager.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <sys/types.h>
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
  void init_swap_chain();

  void init_debug();

public:
  Renderer(GLFWwindow *window);
  ~Renderer();

  DevicesManager &get_device_manager();
};
