#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <sys/types.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class Renderer {

private:
  GLFWwindow *window;
  vk::DynamicLoader dl;

  vk::raii::Context context;
  vk::raii::Instance instance;
  vk::raii::SurfaceKHR surface;

  vk::raii::DebugUtilsMessengerEXT debugMessanger;

  std::unique_ptr<DeviceManager> deviceManager;
  std::unique_ptr<MaterialManager> materialManager;

  std::unique_ptr<BufferManager> bufferManager;

  uint32_t currentFrame;
  uint32_t frameCount; // Total frames rendered, used for AFR

  void init_instance();
  void init_surface();
  void init_device();
  void init_swap_chain();
  void init_materials();

  void init_debug();

  void create_buffers();

  // Helper methods for drawFrame
  bool acquire_next_image(LogicalDevice *device, uint32_t &imageIndex);
  void present_frame(LogicalDevice *device, uint32_t imageIndex);

public:
  Renderer(GLFWwindow *window);
  ~Renderer();

  void reload();
  void drawFrame();

  DeviceManager &get_device_manager();
};
