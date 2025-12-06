#include "logical_device.h"
#include "config.h"
#include "physical_device.h"
#include "swap_chain.h"
#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <thread>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_raii.hpp>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#pragma clang diagnostic pop

LogicalDevice::LogicalDevice(vk::raii::Instance &instance,
                             PhysicalDevice *physicalDevice,
                             uint32_t graphicsQueueIndex)
    : stopThread(false), physicalDevice(physicalDevice), device(nullptr),
      graphicsQueue(nullptr), graphicsQueueIndex(graphicsQueueIndex),
      allocator(VK_NULL_HANDLE) {

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

  thread = std::jthread(&LogicalDevice::thread_loop, this);
}

LogicalDevice::~LogicalDevice() {

  {
    std::lock_guard lock(mutex);
    stopThread = true;

    // clear pending tasks
    while (!taskQueue.empty()) {
      taskQueue.pop();
    }
  }
  conditionVariable.notify_all();

  if (thread.joinable()) {
    try {
      thread.join();
    } catch (const std::system_error &e) {
      std::print("Error joining thread: {}\n", e.what());
    }
  }

  swapChain.reset();

  vmaDestroyAllocator(allocator);

  std::print("Logical device for - {} - destructor executed\n",
             physicalDevice->get_properties().deviceName.data());
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
      try {
        if (task) {
          task();
        }
      } catch (const std::exception &e) {
        std::print("Error executing task in device {0} thread: {1}\n",
                   physicalDevice->get_properties().deviceName.data(),
                   e.what());
      }
      lock.lock();
    }
  }
}

void LogicalDevice::initialize_vma_allocator(vk::raii::Instance &instance) {

  VmaAllocatorCreateInfo allocatorInfo = {
      .flags = 0,
      .physicalDevice = *physicalDevice->get_device(),
      .device = *device,
      .pVulkanFunctions = Config::get_instance().get_vma_vulkan_functions(),
      .instance = *instance,
      .vulkanApiVersion = VK_API_VERSION_1_4};

  // Enable specific features if available
  auto features = Config::get_features(physicalDevice->get_device());

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

void LogicalDevice::initialize_swap_chain(GLFWwindow *window,
                                          vk::raii::SurfaceKHR &surface) {
  swapChain = std::make_unique<SwapChain>(this, window, surface);
  swapChain->create_swap_chain();
  swapChain->create_swap_image_views();
}
void LogicalDevice::initialize_swap_chain(vk::SurfaceFormatKHR format,
                                          vk::Extent2D extent) {
  swapChain = std::make_unique<SwapChain>(this, format, extent);
  swapChain->create_swap_chain();
  swapChain->create_swap_image_views();
}

void LogicalDevice::wait_idle() {
  device.waitIdle();

  // Wait for all queued tasks to complete
  std::promise<void> promise;
  auto future = promise.get_future();

  submit_task([&promise]() { promise.set_value(); });

  future.wait();
  device.waitIdle();
}

PhysicalDevice *LogicalDevice::get_physical_device() const {
  return physicalDevice;
}

const vk::raii::Device &LogicalDevice::get_device() const { return device; }

vk::raii::Queue &LogicalDevice::get_graphics_queue() { return graphicsQueue; }

uint32_t LogicalDevice::get_graphics_queue_index() const {
  return graphicsQueueIndex;
}

SwapChain &LogicalDevice::get_swap_chain() { return *swapChain; }

VmaAllocator LogicalDevice::get_allocator() const { return allocator; }
