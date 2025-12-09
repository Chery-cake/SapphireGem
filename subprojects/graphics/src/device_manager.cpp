#include "config.h"
#include "device_manager.h"
#include "logical_device.h"
#include "physical_device.h"
#include "tasks.h"
#include "vulkan/vulkan.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <print>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

DeviceManager::DeviceManager(GLFWwindow *window, vk::raii::Instance &instance,
                             vk::raii::SurfaceKHR &surface)
    : window(window), instance(instance), surface(surface),
      primaryDevice(nullptr), multiGPUEnabled(false) {}

DeviceManager::~DeviceManager() {
  wait_idle();

  logicalDevices.clear();
  physicalDevices.clear();

  std::print("Devices manager destructor executed\n");
}

PhysicalDevice *DeviceManager::select_primary_device() const {
  PhysicalDevice *bestDevice = nullptr;
  int bestScore = -1;

  for (const auto &device : physicalDevices) {
    if (Config::get_instance().validate_device_requirements(
            device->get_device()) &&
        device->supports_required_features()) {
      int score = device->calculate_score(surface);
      if (score > bestScore) {
        bestScore = score;
        bestDevice = device.get();
      }
    }
  }
  return bestDevice;
}

uint32_t
DeviceManager::find_graphics_queue_index(PhysicalDevice *device) const {
  uint32_t queueIndex;
  if (!device->has_graphic_queue(surface, &queueIndex)) {
    throw std::runtime_error(
        "Device does not support graphics queue with presentation");
  }
  return queueIndex;
}

void DeviceManager::add_device(PhysicalDevice *physicalDevice) {
  if (primaryDevice->get_physical_device() == physicalDevice) {
    return;
  }

  bool equal = false;

  for (auto &secondaryDevice : secondaryDevices) {
    if (secondaryDevice->get_physical_device() == physicalDevice) {
      equal = true;
    }
  }

  if (!equal) {
    try {
      uint32_t secondaryQueueIndex = find_graphics_queue_index(physicalDevice);
      auto logicalDevice = std::make_unique<LogicalDevice>(
          instance, physicalDevice, secondaryQueueIndex);

      logicalDevices.push_back(std::move(logicalDevice));
      secondaryDevices.push_back(logicalDevice.get());

      Tasks::get_instance().add_gpu();

      std::print("Secondary device added: {}\n",
                 physicalDevice->get_properties().deviceName.data());
    } catch (const std::exception &e) {
      std::print("Failed to create logical device for {}: {}\n",
                 physicalDevice->get_properties().deviceName.data(), e.what());
    }
  }
}

void DeviceManager::enumerate_physical_devices() {
  physicalDevices.clear();

  auto devices = instance.enumeratePhysicalDevices();
  for (auto &device : devices) {
    auto physicalDevice = std::make_unique<PhysicalDevice>(std::move(device));
    physicalDevices.push_back(std::move(physicalDevice));
    std::print("Found physical device: {}\n",
               physicalDevices.back()->get_properties().deviceName.data());
  }

  if (physicalDevices.empty()) {
    throw std::runtime_error("No Vulkan-capable devices found!");
  }
  if (physicalDevices.size() > 1) { // TODO properly implement swapchains for
                                    // rendering only for secondery GPUs
    // multiGPUEnabled = true;
  }
}

void DeviceManager::initialize_devices() {
  logicalDevices.clear();
  secondaryDevices.clear();

  // Always create primary device
  auto primaryPhysical = select_primary_device();
  if (!primaryPhysical) {
    throw std::runtime_error("No suitable primary device found!");
  }

  uint32_t queueIndex = find_graphics_queue_index(primaryPhysical);
  auto primary =
      std::make_unique<LogicalDevice>(instance, primaryPhysical, queueIndex);
  primaryDevice = primary.get();
  logicalDevices.push_back(std::move(primary));

  std::print("Primary device selected: {}\n",
             primaryPhysical->get_properties().deviceName.data());

  // For multi-GPU, create logical devices for secondary GPUs
  if (multiGPUEnabled) {
    for (const auto &physicalDevice : physicalDevices) {
      add_device(physicalDevice.get());
    }
  }
}

void DeviceManager::switch_multi_GPU(bool enable) { multiGPUEnabled = enable; }

void DeviceManager::wait_idle() {
  for (auto &device : logicalDevices) {
    device->get_device().waitIdle();
  }
}

void DeviceManager::create_swap_chains() {
  primaryDevice->initialize_swap_chain(window, surface);

  if (multiGPUEnabled) {
    for (auto &device : secondaryDevices) {
      device->initialize_swap_chain(
          primaryDevice->get_swap_chain().get_surface_format(),
          primaryDevice->get_swap_chain().get_extent2D());
    }
  }
}

void DeviceManager::recreate_swap_chain() {

  wait_idle();

  primaryDevice->get_swap_chain().recreate_swap_chain();

  if (multiGPUEnabled) {
    for (auto &device : secondaryDevices) {
      device->get_swap_chain().recreate_swap_chain(
          primaryDevice->get_swap_chain().get_surface_format(),
          primaryDevice->get_swap_chain().get_extent2D());
    }
  }
}

void DeviceManager::create_command_pool() {

  vk::CommandPoolCreateInfo createInfo{
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer};

  primaryDevice->initialize_command_pool(createInfo);

  if (multiGPUEnabled) {
    for (const auto &device : secondaryDevices) {
      device->initialize_command_pool(createInfo);
    }
  }
}

const LogicalDevice *DeviceManager::get_primary_device() const {
  return primaryDevice;
}

const std::vector<LogicalDevice *> &
DeviceManager::get_all_secondary_devices() const {
  static std::vector<LogicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : secondaryDevices) {
    ptrs.push_back(device);
  }
  return ptrs;
}

const std::vector<PhysicalDevice *> &
DeviceManager::get_all_physical_devices() const {
  static std::vector<PhysicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : physicalDevices) {
    ptrs.push_back(device.get());
  }
  return ptrs;
}

const std::vector<LogicalDevice *> &
DeviceManager::get_all_logical_devices() const {
  static std::vector<LogicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : logicalDevices) {
    ptrs.push_back(device.get());
  }
  return ptrs;
}

bool DeviceManager::is_multi_GPU_enabled() const { return multiGPUEnabled; }
