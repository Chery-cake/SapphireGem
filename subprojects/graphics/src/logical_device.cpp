#include "logical_device.h"
#include "buffer.h"
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
#include <vector>
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
      commandPool(nullptr), descriptorPool(nullptr) {

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
  create_descriptor_pool();
  create_sync_objects();

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
      .vulkanApiVersion = Config::get_instance().get_api_version()};

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

void LogicalDevice::create_descriptor_pool() {
  uint32_t maxFrames = Config::get_instance().get_max_frames();

  // Define pool sizes for different descriptor types
  std::vector<vk::DescriptorPoolSize> poolSizes = {
      {vk::DescriptorType::eUniformBuffer, maxFrames * 100},
      {vk::DescriptorType::eStorageBuffer, maxFrames * 100},
      {vk::DescriptorType::eCombinedImageSampler, maxFrames * 100},
      {vk::DescriptorType::eSampledImage, maxFrames * 100},
      {vk::DescriptorType::eStorageImage, maxFrames * 100}};

  vk::DescriptorPoolCreateInfo poolInfo{
      .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
      .maxSets = maxFrames * 100,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data()};

  descriptorPool = device.createDescriptorPool(poolInfo);

  std::print("Descriptor pool created for device: {}\n",
             physicalDevice->get_properties().deviceName.data());
}

void LogicalDevice::create_sync_objects() {
  uint32_t maxFrames = Config::get_instance().get_max_frames();

  imageAvailableSemaphores.reserve(maxFrames);
  renderFinishedSemaphores.reserve(maxFrames);
  inFlightFences.reserve(maxFrames);

  vk::SemaphoreCreateInfo semaphoreInfo{};
  vk::FenceCreateInfo fenceInfo{.flags = vk::FenceCreateFlagBits::eSignaled};

  for (uint32_t i = 0; i < maxFrames; ++i) {
    imageAvailableSemaphores.push_back(device.createSemaphore(semaphoreInfo));
    renderFinishedSemaphores.push_back(device.createSemaphore(semaphoreInfo));
    inFlightFences.push_back(device.createFence(fenceInfo));
  }

  std::print("Synchronization objects created for device: {} ({} frames)\n",
             physicalDevice->get_properties().deviceName.data(), maxFrames);
}

void LogicalDevice::create_swapchain_semaphores() {
  if (!swapChain) {
    throw std::runtime_error("Swapchain must be created before semaphores");
  }

  // Semaphores are already created in create_sync_objects() based on
  // maxFrames. The synchronization model uses per-frame semaphores (not
  // per-image) to properly handle frames in flight. This function maintains
  // API compatibility for existing callers but no longer performs actual
  // semaphore creation.

  std::print("Swapchain created for device: {} ({} images, using {} frame "
             "semaphores)\n",
             physicalDevice->get_properties().deviceName.data(),
             static_cast<uint32_t>(swapChain->get_images().size()),
             imageAvailableSemaphores.size());
}

void LogicalDevice::initialize_swap_chain(GLFWwindow *window,
                                          vk::raii::SurfaceKHR &surface) {
  swapChain = std::make_unique<SwapChain>(this, window, surface);
  swapChain->create_swap_chain();
  swapChain->create_swap_image_views();
  create_swapchain_semaphores();
}
void LogicalDevice::initialize_swap_chain(vk::SurfaceFormatKHR format,
                                          vk::Extent2D extent) {
  swapChain = std::make_unique<SwapChain>(this, format, extent);
  swapChain->create_swap_chain();
  swapChain->create_swap_image_views();
  create_swapchain_semaphores();
}

void LogicalDevice::initialize_command_pool(
    vk::CommandPoolCreateInfo &createInfo) {
  createInfo.queueFamilyIndex = graphicsQueueIndex;
  commandPool = vk::raii::CommandPool(device, createInfo);
}

void LogicalDevice::create_command_buffer() {
  vk::CommandBufferAllocateInfo allocInfo{
      .commandPool = *commandPool,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = Config::get_instance().get_max_frames()};
  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
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

bool LogicalDevice::wait_for_fence(uint32_t frameIndex) {
  auto result =
      device.waitForFences(*inFlightFences[frameIndex], VK_TRUE, UINT64_MAX);
  if (result != vk::Result::eSuccess) {
    std::print("Failed to wait for fence (frame {}): {}\n", frameIndex,
               vk::to_string(result));
    return false;
  }
  return true;
}

void LogicalDevice::reset_fence(uint32_t frameIndex) {
  device.resetFences(*inFlightFences[frameIndex]);
}

void LogicalDevice::begin_command_buffer(uint32_t frameIndex) {
  commandBuffers[frameIndex].reset();
  vk::CommandBufferBeginInfo beginInfo{};
  commandBuffers[frameIndex].begin(beginInfo);
}

void LogicalDevice::end_command_buffer(uint32_t frameIndex) {
  commandBuffers[frameIndex].end();
}

void LogicalDevice::submit_command_buffer(uint32_t frameIndex,
                                          uint32_t semaphoreIndex,
                                          bool withSemaphores) {
  if (withSemaphores) {
    vk::PipelineStageFlags waitStages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &*imageAvailableSemaphores[semaphoreIndex],
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &*commandBuffers[frameIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &*renderFinishedSemaphores[semaphoreIndex]};

    graphicsQueue.submit(submitInfo, *inFlightFences[frameIndex]);
  } else {
    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &*commandBuffers[frameIndex]};

    graphicsQueue.submit(submitInfo, nullptr);
  }
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

const vk::raii::CommandPool &LogicalDevice::get_command_pool() const {
  return commandPool;
}

const vk::raii::DescriptorPool &LogicalDevice::get_descriptor_pool() const {
  return descriptorPool;
}

std::vector<vk::raii::CommandBuffer> &LogicalDevice::get_command_buffers() {
  return commandBuffers;
}

const vk::raii::Semaphore &
LogicalDevice::get_image_available_semaphore(uint32_t imageIndex) const {
  if (imageIndex >= imageAvailableSemaphores.size()) {
    throw std::out_of_range(
        "Frame index out of range for image available semaphore");
  }
  return imageAvailableSemaphores[imageIndex];
}

const vk::raii::Semaphore &
LogicalDevice::get_render_finished_semaphore(uint32_t imageIndex) const {
  if (imageIndex >= renderFinishedSemaphores.size()) {
    throw std::out_of_range(
        "Frame index out of range for render finished semaphore");
  }
  return renderFinishedSemaphores[imageIndex];
}

const vk::raii::Fence &
LogicalDevice::get_in_flight_fence(uint32_t frameIndex) const {
  if (frameIndex >= inFlightFences.size()) {
    throw std::out_of_range("Frame index out of range for in-flight fence");
  }
  return inFlightFences[frameIndex];
}
