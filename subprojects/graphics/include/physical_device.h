#pragma once

#include <cstdint>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

class PhysicalDevice {

private:
  vk::raii::PhysicalDevice device;
  vk::PhysicalDeviceProperties properties;
  vk::PhysicalDeviceFeatures features;
  std::vector<vk::QueueFamilyProperties> queueFamilies;

  void initialize_device();

public:
  PhysicalDevice(vk::raii::PhysicalDevice device);
  ~PhysicalDevice();

  int calculate_score(vk::raii::SurfaceKHR &surface) const;
  bool supports_required_features();
  bool has_graphic_queue(vk::raii::SurfaceKHR &surface,
                         uint32_t *queueIndex = nullptr) const;

  vk::raii::PhysicalDevice &get_device();
  const vk::PhysicalDeviceProperties &get_properties() const;
  const vk::PhysicalDeviceFeatures &get_features() const;
  const std::vector<vk::QueueFamilyProperties> &get_queue_families() const;
};
