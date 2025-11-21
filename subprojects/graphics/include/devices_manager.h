#pragma once

#include "logical_device.h"
#include "physical_device.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class DevicesManager {

private:
  GLFWwindow *window;

  vk::raii::Instance &instance;
  vk::raii::SurfaceKHR &surface;

  std::vector<std::unique_ptr<PhysicalDevice>> physicalDevices;
  std::vector<std::unique_ptr<LogicalDevice>> logicalDevices;
  std::vector<LogicalDevice *> secondaryDevices;
  LogicalDevice *primaryDevice;
  bool multiGPUEnabled;

  PhysicalDevice *select_primary_device() const;
  uint32_t find_graphics_queue_index(PhysicalDevice *device) const;
  void add_device(PhysicalDevice *physicalDevice);

public:
  DevicesManager(GLFWwindow *window, vk::raii::Instance &instance,
                 vk::raii::SurfaceKHR &surface);
  ~DevicesManager();

  void enumerate_physical_devices();
  void initialize_devices();

  // Multi-GPU support
  void switch_multi_GPU(bool enable);
  void wait_idle();

  void create_swap_chains();
  void recreate_swap_chain();

  // Getters
  const LogicalDevice *get_primary_device() const;
  const std::vector<LogicalDevice *> &get_all_secondary_devices() const;
  const std::vector<PhysicalDevice *> &get_all_physical_devices() const;
  const std::vector<LogicalDevice *> &get_all_logical_devices() const;

  bool is_multi_GPU_enabled() const;
};
