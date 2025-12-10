#pragma once

#include "device_manager.h"
#include "logical_device.h"
#include <memory>
#include <mutex>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class DescriptorPoolManager {
public:
  struct DescriptorPoolSizes {
    uint32_t uniformBufferCount = 0;
    uint32_t storageBufferCount = 0;
    uint32_t combinedImageSamplerCount = 0;
    uint32_t storageImageCount = 0;
    uint32_t inputAttachmentCount = 0;
  };

  struct DeviceDescriptorResources {
    std::vector<vk::raii::DescriptorPool> descriptorPools;
    
    DeviceDescriptorResources() = default;
  };

private:
  mutable std::mutex poolMutex;
  
  const DeviceManager *deviceManager;
  std::vector<LogicalDevice *> logicalDevices;
  
  // Per-device descriptor pool resources (one per MAX_FRAMES_IN_FLIGHT)
  std::vector<std::unique_ptr<DeviceDescriptorResources>> deviceResources;
  
  uint32_t maxFramesInFlight;
  uint32_t maxSetsPerPool;
  
  bool create_descriptor_pool(LogicalDevice *device, 
                              vk::raii::DescriptorPool &pool,
                              const DescriptorPoolSizes &sizes);

public:
  DescriptorPoolManager(const DeviceManager *deviceManager, 
                       uint32_t maxFramesInFlight = 2,
                       uint32_t maxSetsPerPool = 1000);
  ~DescriptorPoolManager();

  DescriptorPoolManager(const DescriptorPoolManager &) = delete;
  DescriptorPoolManager &operator=(const DescriptorPoolManager &) = delete;
  DescriptorPoolManager(DescriptorPoolManager &&) = delete;
  DescriptorPoolManager &operator=(DescriptorPoolManager &&) = delete;

  // Initialize descriptor pools for all devices
  bool initialize(const DescriptorPoolSizes &sizes);
  
  // Allocate descriptor sets from the pool
  std::vector<vk::raii::DescriptorSet> 
  allocate_descriptor_sets(vk::raii::DescriptorSetLayout &layout,
                          uint32_t count,
                          uint32_t frameIndex,
                          uint32_t deviceIndex = 0);
  
  // Reset pools (for full reload)
  void reset_pools();
  
  // Get pool for specific device and frame
  vk::raii::DescriptorPool &get_pool(uint32_t deviceIndex, uint32_t frameIndex);
  
  uint32_t get_max_frames_in_flight() const;
};
