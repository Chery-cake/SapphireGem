#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "logical_device.h"
#include "material_manager.h"
#include "object_manager.h"
#include "texture_manager.h"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <sys/types.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace render {

class Renderer {

private:
  GLFWwindow *window;
  vk::detail::DynamicLoader dl;

  vk::raii::Context context;
  vk::raii::Instance instance;
  vk::raii::SurfaceKHR surface;

  vk::raii::DebugUtilsMessengerEXT debugMessanger;

  ObjectManager::MultiGPUConfig gpuConfig;

  std::unique_ptr<device::DeviceManager> deviceManager;
  std::unique_ptr<MaterialManager> materialManager;
  std::unique_ptr<TextureManager> textureManager;

  std::unique_ptr<device::BufferManager> bufferManager;
  std::unique_ptr<ObjectManager> objectManager;

  uint32_t currentFrame;
  uint32_t frameCount; // total frames rendered
  uint32_t currentSemaphoreIndex;

  std::function<void()> postReloadCallback;

  void init_instance();
  void init_surface();
  void init_device();
  void init_swap_chain();

  void init_debug();

  // Helper methods for drawFrame
  bool acquire_next_image(device::LogicalDevice *device, uint32_t &imageIndex,
                          uint32_t &semaphoreIndex);
  void present_frame(device::LogicalDevice *device, uint32_t imageIndex,
                     uint32_t semaphoreIndex);

  // Strategy-specific rendering
  void draw_frame_single_gpu(device::LogicalDevice *device);
  void draw_frame_afr();
  void draw_frame_sfr(uint32_t imageIndex, uint32_t semaphoreIndex);
  void draw_frame_hybrid();
  void draw_frame_multi_queue_streaming();

public:
  Renderer(GLFWwindow *window);
  ~Renderer();

  void reload();
  void draw_frame();

  void set_render_strategy(ObjectManager::RenderStrategy strategy);
  void set_gpu_config(const ObjectManager::MultiGPUConfig &config);

  void set_post_reload_callback(std::function<void()> callback);

  device::DeviceManager &get_device_manager();
  MaterialManager &get_material_manager();
  TextureManager &get_texture_manager();
  device::BufferManager &get_buffer_manager();
  ObjectManager *get_object_manager();
};

} // namespace render
