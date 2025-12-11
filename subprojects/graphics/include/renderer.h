#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "engine3d.h"
#include "material_manager.h"
#include "render_strategy.h"
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
  std::unique_ptr<Engine3D> engine3D; // Optional 3D engine

  uint32_t currentFrame;
  uint32_t frameCount; // Total frames rendered

  // Multi-GPU configuration
  MultiGPUConfig gpuConfig;
  bool useEngine3D; // Whether to use Engine3D for rendering

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

  // Strategy-specific rendering
  void draw_frame_single_gpu();
  void draw_frame_afr();
  void draw_frame_sfr(uint32_t imageIndex);
  void draw_frame_hybrid();
  void draw_frame_multi_queue_streaming();

public:
  Renderer(GLFWwindow *window, bool enableEngine3D = false);
  ~Renderer();

  void reload();
  void drawFrame();

  // Configuration
  void set_render_strategy(RenderStrategy strategy);
  void set_gpu_config(const MultiGPUConfig &config);

  // Access managers
  DeviceManager &get_device_manager();
  MaterialManager &get_material_manager();
  BufferManager &get_buffer_manager();
  Engine3D *get_engine3d(); // Returns nullptr if not enabled
};
