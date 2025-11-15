#pragma once

#include "physical_device.h"
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
  std::shared_ptr<PhysicalDevice> physicalDevice;
  vk::raii::Device device;
  vk::raii::Queue graphicsQueue;
  uint32_t graphicsQueueIndex;

  VmaAllocator allocator;

  // Thread management
  std::jthread thread;
  std::mutex mutex;
  std::condition_variable conditionVariable;
  std::queue<std::function<void()>> taskQueue;
  bool stopThread;

  void thread_loop();

  static VmaVulkanFunctions vmaVulkanFunctions;

  void initialize_vma_allocator(vk::raii::Instance &instance);

public:
  LogicalDevice(vk::raii::Instance &instance,
                std::shared_ptr<PhysicalDevice> physicalDevice,
                uint32_t graphicsQueueIndex);
  ~LogicalDevice();

  void wait_idle();

  std::shared_ptr<PhysicalDevice> get_physical_device() const;
  vk::raii::Device &get_device();
  vk::raii::Queue &get_graphics_queue();
  uint32_t get_graphics_queue_index() const;

  // Task submission
  template <typename F> void submit_task(F &&task) {
    {
      std::lock_guard lock(mutex);
      taskQueue.push(std::forward<F>(task));
    }
    conditionVariable.notify_one();
  }
};
