#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "logical_device.h"
#include "material_manager.h"
#include "object_manager.h"
#include <GLFW/glfw3.h>
#include <cstdint>
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

  ObjectManager::MultiGPUConfig gpuConfig;

  std::unique_ptr<DeviceManager> deviceManager;
  std::unique_ptr<MaterialManager> materialManager;

  std::unique_ptr<BufferManager> bufferManager;
  std::unique_ptr<ObjectManager> objectManager;

  uint32_t currentFrame;
  uint32_t frameCount; // total frames rendered

  void init_instance();
  void init_surface();
  void init_device();
  void init_swap_chain();
  void init_materials();

  void init_debug();

  void create_buffers();

  // Helper methods for drawFrame
  bool acquire_next_image(LogicalDevice *device, uint32_t &imageIndex,
                          uint32_t &semaphoreIndex);
  void present_frame(LogicalDevice *device, uint32_t imageIndex,
                     uint32_t semaphoreIndex);

  // Strategy-specific rendering
  void draw_frame_single_gpu(LogicalDevice *device);
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

  DeviceManager &get_device_manager();
  MaterialManager &get_material_manager();
  BufferManager &get_buffer_manager();
  ObjectManager *get_object_manager();

  // Helper functions to create objects
  RenderObject *create_triangle_2d(const std::string &identifier,
                                   const glm::vec3 &position = glm::vec3(0.0f),
                                   const glm::vec3 &rotation = glm::vec3(0.0f),
                                   const glm::vec3 &scale = glm::vec3(1.0f));
  RenderObject *create_cube_3d(const std::string &identifier,
                               const glm::vec3 &position = glm::vec3(0.0f),
                               const glm::vec3 &rotation = glm::vec3(0.0f),
                               const glm::vec3 &scale = glm::vec3(1.0f));
};
