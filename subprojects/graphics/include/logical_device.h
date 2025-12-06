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

  void thread_loop();

  void initialize_vma_allocator(vk::raii::Instance &instance);

public:
  LogicalDevice(vk::raii::Instance &instance, PhysicalDevice *physicalDevice,
                uint32_t graphicsQueueIndex);
  ~LogicalDevice();

  void initialize_swap_chain(GLFWwindow *window, vk::raii::SurfaceKHR &surface);
  void initialize_swap_chain(vk::SurfaceFormatKHR format, vk::Extent2D extent);

  void wait_idle();

  PhysicalDevice *get_physical_device() const;
  const vk::raii::Device &get_device() const;
  vk::raii::Queue &get_graphics_queue();
  uint32_t get_graphics_queue_index() const;
  SwapChain &get_swap_chain();
  VmaAllocator get_allocator() const;

  // Task submission
  template <typename F> void submit_task(F &&task) {
    {
      std::lock_guard lock(mutex);
      taskQueue.push(std::forward<F>(task));
    }
    conditionVariable.notify_one();
  }
};
