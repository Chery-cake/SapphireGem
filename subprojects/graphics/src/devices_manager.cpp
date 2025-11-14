#include "devices_manager.h"
#include "config.h"
#include "logical_device.h"
#include "physical_device.h"
#include "tasks.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <print>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

DevicesManager::DevicesManager(GLFWwindow *window, vk::raii::Instance &instance,
                               vk::raii::SurfaceKHR &surface)
    : window(window), instance(instance), surface(surface), physicalDevices({}),
      logicalDevices({}), primaryDevice(nullptr), multiGPUEnabled(false),
      swapChain(nullptr) {}

DevicesManager::~DevicesManager() {
  wait_idle();

  swapChainImageViews.clear();
  swapChainImages.clear();
  swapChain.clear();

  logicalDevices.clear();
  primaryDevice.reset();
  physicalDevices.clear();

  std::print("Devices manager destructor executed\n");
}

std::shared_ptr<PhysicalDevice> DevicesManager::select_primary_device() const {
  std::shared_ptr<PhysicalDevice> bestDevice = nullptr;
  int bestScore = -1;

  for (const auto &device : physicalDevices) {
    if (Config::get_instance().validate_device_requirements(
            device->get_device()) &&
        device->supports_required_features()) {
      int score = device->calculate_score(surface);
      if (score > bestScore) {
        bestScore = score;
        bestDevice = device;
      }
    }
  }
  return bestDevice;
}

uint32_t DevicesManager::find_graphics_queue_index(
    std::shared_ptr<PhysicalDevice> device) const {
  uint32_t queueIndex;
  if (!device->has_graphic_queue(surface, &queueIndex)) {
    throw std::runtime_error(
        "Device does not support graphics queue with presentation");
  }
  return queueIndex;
}

void DevicesManager::add_device(
    std::shared_ptr<PhysicalDevice> physicalDevice) {
  bool equal = false;

  for (auto &logicalDevice : logicalDevices) {
    if (logicalDevice.get()->get_physical_device() == physicalDevice) {
      equal = true;
    }
  }

  if (!equal) {
    try {
      uint32_t secondaryQueueIndex = find_graphics_queue_index(physicalDevice);
      auto logicalDevice = std::make_shared<LogicalDevice>(
          instance, physicalDevice, secondaryQueueIndex);
      logicalDevices.push_back(logicalDevice);

      Tasks::get_instance().add_gpu();

      std::print("Secondary device added: {}\n",
                 physicalDevice->get_properties().deviceName.data());
    } catch (const std::exception &e) {
      std::print("Failed to create logical device for {}: {}\n",
                 physicalDevice->get_properties().deviceName.data(), e.what());
    }
  }
}

void DevicesManager::enumerate_physical_devices() {
  physicalDevices.clear();

  auto devices = instance.enumeratePhysicalDevices();
  for (auto &device : devices) {
    auto physicalDevice = std::make_shared<PhysicalDevice>(std::move(device));
    physicalDevices.push_back(physicalDevice);
    std::print("Found physical device: {}\n",
               physicalDevice->get_properties().deviceName.data());
  }

  if (physicalDevices.empty()) {
    throw std::runtime_error("No Vulkan-capable devices found!");
  }
  if (physicalDevices.size() > 1) {
    multiGPUEnabled = true;
  }
}

void DevicesManager::initialize_devices() {
  logicalDevices.clear();

  // Always create primary device
  auto primaryPhysical = select_primary_device();
  if (!primaryPhysical) {
    throw std::runtime_error("No suitable primary device found!");
  }

  uint32_t queueIndex = find_graphics_queue_index(primaryPhysical);
  primaryDevice =
      std::make_shared<LogicalDevice>(instance, primaryPhysical, queueIndex);
  logicalDevices.push_back(primaryDevice);

  std::print("Primary device selected: {}\n",
             primaryPhysical->get_properties().deviceName.data());

  // For multi-GPU, create logical devices for secondary GPUs
  if (multiGPUEnabled) {
    for (const auto &physicalDevice : physicalDevices) {
      add_device(physicalDevice);
    }
  }
}

void DevicesManager::switch_multi_GPU(bool enable) { multiGPUEnabled = enable; }

void DevicesManager::wait_idle() {
  for (auto &device : logicalDevices) {
    device->get_device().waitIdle();
  }
}

void DevicesManager::create_swap_chain() {
  auto surfaceCapabilities = primaryDevice->get_physical_device()
                                 ->get_device()
                                 .getSurfaceCapabilitiesKHR(surface);

  auto swapChainCurrentExtent = [capabilities = &surfaceCapabilities,
                                 this]() -> vk::Extent2D {
    if (capabilities->currentExtent.width != 0xFFFFFFFF) {
      return capabilities->currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    return {std::clamp<uint32_t>(width, capabilities->minImageExtent.width,
                                 capabilities->maxImageExtent.width),
            std::clamp<uint32_t>(height, capabilities->minImageExtent.height,
                                 capabilities->maxImageExtent.height)};
  }();

  swapChainSurfaceFormat = [availableFormats =
                                primaryDevice->get_physical_device()
                                    ->get_device()
                                    .getSurfaceFormatsKHR(surface)]() {
    assert(!availableFormats.empty());
    const auto iterator =
        std::ranges::find_if(availableFormats, [](const auto &format) {
          return format.format == vk::Format::eB8G8R8A8Srgb &&
                 format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
    return iterator != availableFormats.end() ? *iterator : availableFormats[0];
  }();

  auto swapChainPresentMode = [availablePresentModes =
                                   primaryDevice->get_physical_device()
                                       ->get_device()
                                       .getSurfacePresentModesKHR(surface)]() {
    assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
      return presentMode == vk::PresentModeKHR::eFifo;
    }));
    return std::ranges::any_of(availablePresentModes,
                               [](const vk::PresentModeKHR value) {
                                 return vk::PresentModeKHR::eMailbox == value;
                               })
               ? vk::PresentModeKHR::eMailbox
               : vk::PresentModeKHR::eFifo;
  }();

  vk::SwapchainCreateInfoKHR swapChainCreateInfo{
      .surface = surface,
      .minImageCount = ((surfaceCapabilities.maxImageCount > 0) &&
                        (surfaceCapabilities.maxImageCount <
                         surfaceCapabilities.minImageCount))
                           ? surfaceCapabilities.maxImageCount
                           : surfaceCapabilities.minImageCount,
      .imageFormat = swapChainSurfaceFormat.format,
      .imageColorSpace = swapChainSurfaceFormat.colorSpace,
      .imageExtent = swapChainCurrentExtent,
      .imageArrayLayers = 1,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
      .imageSharingMode = vk::SharingMode::eExclusive,
      .preTransform = surfaceCapabilities.currentTransform,
      .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
      .presentMode = swapChainPresentMode,
      .clipped = true};

  swapChain =
      vk::raii::SwapchainKHR(primaryDevice->get_device(), swapChainCreateInfo);
  swapChainImages = swapChain.getImages();
}

void DevicesManager::recreate_swap_chain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }

  wait_idle();

  swapChainImages.clear();
  swapChain.clear();

  create_swap_chain();

  swapChainImages.clear();

  create_swap_image_views();
}

void DevicesManager::create_swap_image_views() {
  swapChainImageViews.clear();

  vk::ImageViewCreateInfo imageViewCreateInfo{
      .viewType = vk::ImageViewType::e2D,
      .format = swapChainSurfaceFormat.format,
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  for (auto image : swapChainImages) {
    imageViewCreateInfo.image = image;
    swapChainImageViews.emplace_back(primaryDevice->get_device(),
                                     imageViewCreateInfo);
  }
}

std::shared_ptr<LogicalDevice> DevicesManager::get_primary_device() const {
  return primaryDevice;
}

const std::vector<std::shared_ptr<PhysicalDevice>> &
DevicesManager::get_all_physical_devices() const {
  return physicalDevices;
}

const std::vector<std::shared_ptr<LogicalDevice>> &
DevicesManager::get_all_logical_devices() const {
  return logicalDevices;
}

bool DevicesManager::is_multi_GPU_enabled() const { return multiGPUEnabled; }
