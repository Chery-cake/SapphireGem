#pragma once

#include "logical_device.h"
#include "physical_device.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace device {

class DeviceManager {

private:
  std::mutex deviceMutex;

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
  DeviceManager(GLFWwindow *window, vk::raii::Instance &instance,
                vk::raii::SurfaceKHR &surface);
  ~DeviceManager();

  void enumerate_physical_devices();
  void initialize_devices();

  // Multi-GPU support
  void switch_multi_GPU(bool enable);
  void wait_idle();

  void create_swap_chains();
  void recreate_swap_chain();

  void create_command_pool();

  // Getters
  const LogicalDevice *get_primary_device() const;
  const std::vector<LogicalDevice *> &get_all_secondary_devices() const;
  const std::vector<PhysicalDevice *> &get_all_physical_devices() const;
  const std::vector<LogicalDevice *> &get_all_logical_devices() const;

  bool is_multi_GPU_enabled() const;
};

} // namespace device
