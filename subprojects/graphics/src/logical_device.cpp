#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#pragma clang diagnostic pop

#include "config.h"
#include "logical_device.h"
#include "physical_device.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <print>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

VmaVulkanFunctions LogicalDevice::vmaVulkanFunctions = {};

LogicalDevice::LogicalDevice(vk::raii::Instance &instance,
                             std::shared_ptr<PhysicalDevice> physicalDevice,
                             uint32_t graphicsQueueIndex)
    : physicalDevice(physicalDevice), device(nullptr), graphicsQueue(nullptr),
      graphicsQueueIndex(graphicsQueueIndex), allocator(VK_NULL_HANDLE),
      thread(&LogicalDevice::thread_loop, this), stopThread(false) {

  // Query for required features
  auto featureChain = Config::get_features();

  float queuePriority = 0.0f;
  vk::DeviceQueueCreateInfo deviceQueueCreateInfo{
      .queueFamilyIndex = graphicsQueueIndex,
      .queueCount = 1,
      .pQueuePriorities = &queuePriority};

  vk::DeviceCreateInfo deviceCreateInfo{
      .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &deviceQueueCreateInfo,
      .enabledExtensionCount = static_cast<uint32_t>(
          Config::get_instance().get_device_extension().size()),
      .ppEnabledExtensionNames =
          Config::get_instance().get_device_extension().data()};

  device = vk::raii::Device(physicalDevice->get_device(), deviceCreateInfo);
  graphicsQueue = vk::raii::Queue(device, graphicsQueueIndex, 0);

  std::print("Created logical device: {}\n",
             physicalDevice->get_properties().deviceName.data());

  initialize_vma_allocator(instance);
}

LogicalDevice::~LogicalDevice() {

  {
    std::lock_guard lock(mutex);
    stopThread = true;
  }
  conditionVariable.notify_all();

  thread.join();

  vmaDestroyAllocator(allocator);

  std::print("Logical device destructor executed\n");
}

void LogicalDevice::thread_loop() {
  std::unique_lock lock(mutex);

  while (!stopThread) {
    conditionVariable.wait(
        lock, [this]() { return stopThread || !taskQueue.empty(); });

    while (!taskQueue.empty() && !stopThread) {
      auto task = std::move(taskQueue.front());
      taskQueue.pop();

      // Unlock while executing task to allow new tasks to be submitted
      lock.unlock();
      task();
      lock.lock();
    }
  }
}

void LogicalDevice::initialize_vma_allocator(vk::raii::Instance &instance) {
  // Set up VMA function pointers
  vmaVulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  vmaVulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_4;
  allocatorInfo.physicalDevice = *physicalDevice->get_device();
  allocatorInfo.device = *device;
  allocatorInfo.instance = *instance;
  allocatorInfo.pVulkanFunctions = &vmaVulkanFunctions;

  // Enable specific features if available
  auto features = Config::get_features(physicalDevice->get_device());
  allocatorInfo.flags = 0;

  if (features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress) {
    allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
  }

  VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to create VMA allocator!");
  }

  std::print("VMA allocator initialized for device: {}\n",
             physicalDevice->get_properties().deviceName.data());
}

void LogicalDevice::wait_idle() {
  device.waitIdle();
  submit_task([]() {});
  device.waitIdle();
}

std::shared_ptr<PhysicalDevice> LogicalDevice::get_physical_device() const {
  return physicalDevice;
}

vk::raii::Device &LogicalDevice::get_device() { return device; }

vk::raii::Queue &LogicalDevice::get_graphics_queue() { return graphicsQueue; }

uint32_t LogicalDevice::get_graphics_queue_index() const {
  return graphicsQueueIndex;
}
