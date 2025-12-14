#pragma once

#include "physical_device.h"
#include "swap_chain.h"
#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

class LogicalDevice {
private:
  // Thread management
  std::mutex mutex;
  std::condition_variable conditionVariable;
  std::queue<std::function<void()>> taskQueue;
  bool stopThread;
  std::jthread thread;

  // Vulkan objects
  PhysicalDevice *physicalDevice;
  vk::raii::Device device;
  vk::raii::Queue graphicsQueue;
  uint32_t graphicsQueueIndex;

  VmaAllocator allocator;

  std::unique_ptr<SwapChain> swapChain;
  vk::raii::CommandPool commandPool;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  vk::raii::DescriptorPool descriptorPool;

  // Synchronization primitives
  std::vector<vk::raii::Semaphore> imageAvailableSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;

  void thread_loop();
  void initialize_vma_allocator(vk::raii::Instance &instance);
  void create_descriptor_pool();
  void create_sync_objects();
  void create_swapchain_semaphores();

public:
  LogicalDevice(vk::raii::Instance &instance, PhysicalDevice *physicalDevice,
                uint32_t graphicsQueueIndex);
  ~LogicalDevice();

  void initialize_swap_chain(GLFWwindow *window, vk::raii::SurfaceKHR &surface);
  void initialize_swap_chain(vk::SurfaceFormatKHR format, vk::Extent2D extent);

  void initialize_command_pool(vk::CommandPoolCreateInfo &createInfo);
  void create_command_buffer();

  void wait_idle();
  template <typename F> void submit_task(F &&task);

  // Frame rendering methods
  bool wait_for_fence(uint32_t frameIndex);
  void reset_fence(uint32_t frameIndex);
  void begin_command_buffer(uint32_t frameIndex);
  void end_command_buffer(uint32_t frameIndex);
  void submit_command_buffer(uint32_t frameIndex, uint32_t imageIndex, bool withSemaphores);

  PhysicalDevice *get_physical_device() const;
  const vk::raii::Device &get_device() const;
  vk::raii::Queue &get_graphics_queue();
  uint32_t get_graphics_queue_index() const;
  SwapChain &get_swap_chain();

  VmaAllocator get_allocator() const;
  const vk::raii::CommandPool &get_command_pool() const;
  const vk::raii::DescriptorPool &get_descriptor_pool() const;
  std::vector<vk::raii::CommandBuffer> &get_command_buffers();

  // Synchronization getters
  const vk::raii::Semaphore &
  get_image_available_semaphore(uint32_t imageIndex) const;
  const vk::raii::Semaphore &
  get_render_finished_semaphore(uint32_t imageIndex) const;
  const vk::raii::Fence &get_in_flight_fence(uint32_t frameIndex) const;
};

template <typename F> void LogicalDevice::submit_task(F &&task) {
  {
    std::lock_guard lock(mutex);
    taskQueue.push(std::forward<F>(task));
  }
  conditionVariable.notify_one();
};
