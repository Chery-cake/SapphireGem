#include "device_manager.h"
#include "config.h"
#include "logical_device.h"
#include "physical_device.h"
#include "tasks.h"
#include "vulkan/vulkan.hpp"
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <print>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

device::DeviceManager::DeviceManager(GLFWwindow *window,
                                     vk::raii::Instance &instance,
                                     vk::raii::SurfaceKHR &surface)
    : window(window), instance(instance), surface(surface),
      primaryDevice(nullptr), multiGPUEnabled(false) {}

device::DeviceManager::~DeviceManager() {
  wait_idle();

  logicalDevices.clear();
  physicalDevices.clear();

  std::print("Devices manager destructor executed\n");
}

device::PhysicalDevice *device::DeviceManager::select_primary_device() const {
  PhysicalDevice *bestDevice = nullptr;
  int bestScore = -1;

  std::print("Selecting primary device from {} candidates...\n",
             physicalDevices.size());

  for (const auto &device : physicalDevices) {
    std::print("\nEvaluating device: {}\n",
               device->get_properties().deviceName.data());

    // Validate device requirements
    try {
      if (!general::Config::get_instance().validate_device_requirements(
              device->get_device())) {
        std::print("Device failed validation requirements\n");
        continue;
      }
    } catch (const std::exception &e) {
      std::print("Device validation error: {}\n", e.what());
      continue;
    }

    // Check optional extensions
    general::Config::get_instance().check_and_enable_optional_device_extensions(
        device->get_device());

    // Check required features
    if (!device->supports_required_features()) {
      std::print("Device missing required features\n");
      continue;
    }

    int score = device->calculate_score(surface);
    std::print("Device score: {}\n", score);

    if (score > bestScore) {
      bestScore = score;
      bestDevice = device.get();
      std::print("✓ New best device candidate\n");
    }
  }

  if (bestDevice) {
    std::print("\nSelected primary device: {} (score: {})\n",
               bestDevice->get_properties().deviceName.data(), bestScore);
  }

  return bestDevice;
}

uint32_t
device::DeviceManager::find_graphics_queue_index(PhysicalDevice *device) const {
  uint32_t queueIndex;

  std::print("Finding graphics queue for device: {}\n",
             device->get_properties().deviceName.data());

  auto queueFamilies = device->get_queue_families();
  std::print("Available queue families: {}\n", queueFamilies.size());

  for (uint32_t i = 0; i < queueFamilies.size(); i++) {
    std::print("Queue family {}: flags={}, count={}\n", i,
               to_string(queueFamilies[i].queueFlags),
               queueFamilies[i].queueCount);
  }

  if (!device->has_graphic_queue(surface, &queueIndex)) {
    throw std::runtime_error(
        "Device does not support graphics queue with presentation");
  }

  std::print("✓ Using graphics queue family index: {}\n", queueIndex);

  return queueIndex;
}

void device::DeviceManager::add_device(PhysicalDevice *physicalDevice) {
  std::lock_guard<std::mutex> lock(deviceMutex);

  if (primaryDevice->get_physical_device() == physicalDevice) {
    std::print("Skipping primary device (already initialized): {}\n",
               physicalDevice->get_properties().deviceName.data());
    return;
  }

  bool equal = false;

  for (auto &secondaryDevice : secondaryDevices) {
    if (secondaryDevice->get_physical_device() == physicalDevice) {
      equal = true;
      break;
    }
  }

  if (!equal) {
    try {
      std::print("Initializing secondary device: {}\n",
                 physicalDevice->get_properties().deviceName.data());

      uint32_t secondaryQueueIndex = find_graphics_queue_index(physicalDevice);
      auto logicalDevice = std::make_unique<LogicalDevice>(
          instance, physicalDevice, secondaryQueueIndex);

      auto devicePtr = logicalDevice.get();
      logicalDevices.push_back(std::move(logicalDevice));
      secondaryDevices.push_back(std::move(devicePtr));

      Tasks::get_instance().add_gpu();

      std::print("✓ Secondary device added: {}\n",
                 physicalDevice->get_properties().deviceName.data());
    } catch (const std::exception &e) {
      std::print("Failed to create logical device for {}: {}\n",
                 physicalDevice->get_properties().deviceName.data(), e.what());
    }
  } else {
    std::print("Secondary device already initialized: {}\n",
               physicalDevice->get_properties().deviceName.data());
  }
}

void device::DeviceManager::enumerate_physical_devices() {
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

  std::print("Found {} physical device(s)\n", physicalDevices.size());

  if (physicalDevices.size() > 1) {
    std::print("Multiple GPUs detected - multi-GPU support available\n");
    // Multi-GPU will be enabled when switch_multi_GPU(true) is called
  }
}

void device::DeviceManager::initialize_devices() {
  logicalDevices.clear();
  secondaryDevices.clear();

  std::print("Initializing devices (multi-GPU: {})\n",
             multiGPUEnabled ? "enabled" : "disabled");

  // Always create primary device
  auto primaryPhysical = select_primary_device();
  if (!primaryPhysical) {
    throw std::runtime_error("No suitable primary device found!");
  }

  uint32_t queueIndex = find_graphics_queue_index(primaryPhysical);

  std::print("Creating primary logical device...\n");
  auto primary =
      std::make_unique<LogicalDevice>(instance, primaryPhysical, queueIndex);
  primaryDevice = primary.get();
  logicalDevices.push_back(std::move(primary));

  std::print("✓ Primary device initialized: {}\n",
             primaryPhysical->get_properties().deviceName.data());

  // For multi-GPU, create logical devices for secondary GPUs
  if (multiGPUEnabled) {
    std::print("Initializing secondary devices...\n");
    for (const auto &physicalDevice : physicalDevices) {
      add_device(physicalDevice.get());
    }
    std::print("Multi-GPU initialization complete: {} secondary device(s)\n",
               secondaryDevices.size());
  }
}

void device::DeviceManager::switch_multi_GPU(bool enable) {
  std::lock_guard<std::mutex> lock(deviceMutex);
  multiGPUEnabled = enable;
}

void device::DeviceManager::wait_idle() {
  std::lock_guard<std::mutex> lock(deviceMutex);
  for (auto &device : logicalDevices) {
    device->get_device().waitIdle();
  }
}

void device::DeviceManager::create_swap_chains() {
  std::print("Creating swap chains...\n");

  primaryDevice->initialize_swap_chain(window, surface);
  std::print("✓ Primary device swap chain created\n");

  if (multiGPUEnabled) {
    std::print("Creating swap chains for {} secondary device(s)...\n",
               secondaryDevices.size());
    for (auto &device : secondaryDevices) {
      try {
        device->initialize_swap_chain(
            primaryDevice->get_swap_chain().get_surface_format(),
            primaryDevice->get_swap_chain().get_extent2D());
        std::print(
            "✓ Secondary device swap chain created: {}\n",
            device->get_physical_device()->get_properties().deviceName.data());
      } catch (const std::exception &e) {
        std::fprintf(
            stderr, "✗ Failed to create swap chain for %s: %s\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
      }
    }
  }
}

void device::DeviceManager::recreate_swap_chain() {

  std::print("Recreating swap chains...\n");
  wait_idle();

  primaryDevice->get_swap_chain().recreate_swap_chain();
  // Recreate semaphores to match the new swapchain image count
  primaryDevice->create_swapchain_semaphores();
  std::print("✓ Primary device swap chain recreated\n");

  if (multiGPUEnabled) {
    for (auto &device : secondaryDevices) {
      try {
        device->get_swap_chain().recreate_swap_chain(
            primaryDevice->get_swap_chain().get_surface_format(),
            primaryDevice->get_swap_chain().get_extent2D());
        device->create_swapchain_semaphores();
        std::print(
            "✓ Secondary device swap chain recreated: {}\n",
            device->get_physical_device()->get_properties().deviceName.data());
      } catch (const std::exception &e) {
        std::fprintf(
            stderr, "✗ Failed to recreate swap chain for %s: %s\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
      }
    }
  }
}

void device::DeviceManager::create_command_pool() {

  vk::CommandPoolCreateInfo createInfo{
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer};

  std::print("Creating command pools and buffers...\n");
  primaryDevice->initialize_command_pool(createInfo);
  std::print("✓ Primary device command pool created\n");
  primaryDevice->create_command_buffer();
  std::print("✓ Primary device command buffers created\n");

  if (multiGPUEnabled) {
    for (const auto &device : secondaryDevices) {
      try {
        device->initialize_command_pool(createInfo);
        device->create_command_buffer();
        std::print(
            "✓ Secondary device command pool and buffers created: {}\n",
            device->get_physical_device()->get_properties().deviceName.data());
      } catch (const std::exception &e) {
        std::fprintf(
            stderr, "✗ Failed to create command pool and buffers for %s: %s\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
      }
    }
  }
}

const device::LogicalDevice *device::DeviceManager::get_primary_device() const {
  return primaryDevice;
}

const std::vector<device::LogicalDevice *> &
device::DeviceManager::get_all_secondary_devices() const {
  static std::vector<LogicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : secondaryDevices) {
    ptrs.push_back(device);
  }
  return ptrs;
}

const std::vector<device::PhysicalDevice *> &
device::DeviceManager::get_all_physical_devices() const {
  static std::vector<PhysicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : physicalDevices) {
    ptrs.push_back(device.get());
  }
  return ptrs;
}

const std::vector<device::LogicalDevice *> &
device::DeviceManager::get_all_logical_devices() const {
  static std::vector<LogicalDevice *> ptrs;
  ptrs.clear();
  for (const auto &device : logicalDevices) {
    ptrs.push_back(device.get());
  }
  return ptrs;
}

bool device::DeviceManager::is_multi_GPU_enabled() const {
  return multiGPUEnabled;
}
