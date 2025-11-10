#include "physical_device.h"
#include "config.h"
#include "vulkan/vulkan.hpp"
#include <print>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

PhysicalDevice::PhysicalDevice(vk::raii::PhysicalDevice device)
    : device(device), properties(device.getProperties()),
      features(device.getFeatures()),
      queueFamilies(device.getQueueFamilyProperties()) {}

PhysicalDevice::~PhysicalDevice() {
  std::print("Physical device destructor executed\n");
}

int PhysicalDevice::calculate_score(vk::raii::SurfaceKHR &surface) const {
  int score = 0;

  // Discrete GPUs get highest priority
  if (properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
    score += 1000;
  }
  // Integrated GPUs
  else if (properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu) {
    score += 100;
  }

  // VRAM size bonus
  score += static_cast<int>(properties.limits.maxImageDimension2D / 1000);

  // Geometry shader support
  if (features.geometryShader) {
    score += 10;
  }

  // Graphics queue with presentation support
  uint32_t queueIndex;
  if (has_graphic_queue(surface, &queueIndex)) {
    score += 500;
  }

  return score;
}

bool PhysicalDevice::supports_required_features() {
  auto features2 = Config::get_features(device);

  return features2.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
         features2.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>()
             .extendedDynamicState;
}

bool PhysicalDevice::has_graphic_queue(vk::raii::SurfaceKHR &surface,
                                       uint32_t *queueIndex) const {
  for (uint32_t i = 0; i < queueFamilies.size(); i++) {
    if ((queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) &&
        device.getSurfaceSupportKHR(i, *surface)) {
      if (queueIndex)
        *queueIndex = i;
      return true;
    }
  }
  return false;
}

vk::raii::PhysicalDevice &PhysicalDevice::get_device() { return device; }

const vk::PhysicalDeviceProperties &PhysicalDevice::get_properties() const {
  return properties;
}

const vk::PhysicalDeviceFeatures &PhysicalDevice::get_features() const {
  return features;
}

const std::vector<vk::QueueFamilyProperties> &
PhysicalDevice::get_queue_families() const {
  return queueFamilies;
}
