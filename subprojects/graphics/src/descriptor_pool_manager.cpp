#include "descriptor_pool_manager.h"
#include "device_manager.h"
#include "logical_device.h"
#include <memory>
#include <mutex>
#include <print>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

DescriptorPoolManager::DescriptorPoolManager(const DeviceManager *deviceManager,
                                             uint32_t maxFramesInFlight,
                                             uint32_t maxSetsPerPool)
    : deviceManager(deviceManager), maxFramesInFlight(maxFramesInFlight),
      maxSetsPerPool(maxSetsPerPool) {
  
  logicalDevices = deviceManager->get_all_logical_devices();
  
  deviceResources.reserve(logicalDevices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<DeviceDescriptorResources>());
  }
  
  std::print("DescriptorPoolManager created for {} device(s), {} frames in flight\n",
             logicalDevices.size(), maxFramesInFlight);
}

DescriptorPoolManager::~DescriptorPoolManager() {
  std::lock_guard lock(poolMutex);
  
  for (auto &resources : deviceResources) {
    resources.reset();
  }
  deviceResources.clear();
  
  std::print("DescriptorPoolManager destructor executed\n");
}

bool DescriptorPoolManager::create_descriptor_pool(
    LogicalDevice *device, vk::raii::DescriptorPool &pool,
    const DescriptorPoolSizes &sizes) {
  try {
    std::vector<vk::DescriptorPoolSize> poolSizes;
    
    if (sizes.uniformBufferCount > 0) {
      poolSizes.push_back({vk::DescriptorType::eUniformBuffer,
                          sizes.uniformBufferCount * maxFramesInFlight});
    }
    if (sizes.storageBufferCount > 0) {
      poolSizes.push_back({vk::DescriptorType::eStorageBuffer,
                          sizes.storageBufferCount * maxFramesInFlight});
    }
    if (sizes.combinedImageSamplerCount > 0) {
      poolSizes.push_back({vk::DescriptorType::eCombinedImageSampler,
                          sizes.combinedImageSamplerCount * maxFramesInFlight});
    }
    if (sizes.storageImageCount > 0) {
      poolSizes.push_back({vk::DescriptorType::eStorageImage,
                          sizes.storageImageCount * maxFramesInFlight});
    }
    if (sizes.inputAttachmentCount > 0) {
      poolSizes.push_back({vk::DescriptorType::eInputAttachment,
                          sizes.inputAttachmentCount * maxFramesInFlight});
    }
    
    if (poolSizes.empty()) {
      std::print("Warning: Creating descriptor pool with no descriptors\n");
      poolSizes.push_back({vk::DescriptorType::eUniformBuffer, 1});
    }
    
    vk::DescriptorPoolCreateInfo poolInfo{
        .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
        .maxSets = maxSetsPerPool,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()};
    
    pool = device->get_device().createDescriptorPool(poolInfo);
    
    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create descriptor pool for device {}: {}\n",
               device->get_physical_device()->get_properties().deviceName.data(),
               e.what());
    return false;
  }
}

bool DescriptorPoolManager::initialize(const DescriptorPoolSizes &sizes) {
  std::lock_guard lock(poolMutex);
  
  std::print("Initializing descriptor pools with sizes - UBO: {}, SSBO: {}, "
             "Sampler: {}, Storage Image: {}, Input Attachment: {}\n",
             sizes.uniformBufferCount, sizes.storageBufferCount,
             sizes.combinedImageSamplerCount, sizes.storageImageCount,
             sizes.inputAttachmentCount);
  
  bool allSuccess = true;
  
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    auto *device = logicalDevices[i];
    auto &resources = *deviceResources[i];
    
    // Create one descriptor pool per frame in flight
    resources.descriptorPools.clear();
    resources.descriptorPools.reserve(maxFramesInFlight);
    
    for (uint32_t frame = 0; frame < maxFramesInFlight; ++frame) {
      vk::raii::DescriptorPool pool(nullptr);
      if (!create_descriptor_pool(device, pool, sizes)) {
        std::print("Failed to create descriptor pool for device {} frame {}\n",
                   i, frame);
        allSuccess = false;
        break;
      }
      resources.descriptorPools.push_back(std::move(pool));
    }
    
    if (allSuccess) {
      std::print("✓ Descriptor pools created for device {}: {}\n", i,
                 device->get_physical_device()->get_properties().deviceName.data());
    }
  }
  
  if (allSuccess) {
    std::print("✓ All descriptor pools initialized successfully\n");
  }
  
  return allSuccess;
}

std::vector<vk::raii::DescriptorSet>
DescriptorPoolManager::allocate_descriptor_sets(
    vk::raii::DescriptorSetLayout &layout, uint32_t count,
    uint32_t frameIndex, uint32_t deviceIndex) {
  std::lock_guard lock(poolMutex);
  
  if (deviceIndex >= deviceResources.size()) {
    throw std::runtime_error("Invalid device index");
  }
  
  auto &resources = *deviceResources[deviceIndex];
  
  if (resources.descriptorPools.empty()) {
    throw std::runtime_error("Descriptor pools not initialized");
  }
  
  if (frameIndex >= resources.descriptorPools.size()) {
    throw std::runtime_error("Invalid frame index");
  }
  
  std::vector<vk::raii::DescriptorSet> allSets;
  allSets.reserve(count);
  
  // Allocate from the pool for the specified frame
  auto &pool = resources.descriptorPools[frameIndex];
  
  std::vector<vk::DescriptorSetLayout> layouts(count, *layout);
  vk::DescriptorSetAllocateInfo allocInfo{
      .descriptorPool = *pool,
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data()};
  
  try {
    allSets = logicalDevices[deviceIndex]->get_device().allocateDescriptorSets(
        allocInfo);
    
    std::print("✓ Allocated {} descriptor set(s) for device {} frame {}\n", 
               count, deviceIndex, frameIndex);
  } catch (const std::exception &e) {
    std::print("Failed to allocate descriptor sets for device {} frame {}: {}\n",
               deviceIndex, frameIndex, e.what());
    throw;
  }
  
  return allSets;
}

void DescriptorPoolManager::reset_pools() {
  std::lock_guard lock(poolMutex);
  
  std::print("Resetting all descriptor pools...\n");
  
  for (size_t i = 0; i < deviceResources.size(); ++i) {
    auto &resources = *deviceResources[i];
    for (auto &pool : resources.descriptorPools) {
      try {
        pool.reset();
        std::print("✓ Reset pool for device {} \n", i);
      } catch (const std::exception &e) {
        std::print("Warning: Failed to reset pool for device {}: {}\n", i,
                   e.what());
      }
    }
  }
  
  std::print("✓ All descriptor pools reset\n");
}

vk::raii::DescriptorPool &
DescriptorPoolManager::get_pool(uint32_t deviceIndex, uint32_t frameIndex) {
  std::lock_guard lock(poolMutex);
  
  if (deviceIndex >= deviceResources.size()) {
    throw std::runtime_error("Invalid device index");
  }
  
  auto &resources = *deviceResources[deviceIndex];
  
  if (frameIndex >= resources.descriptorPools.size()) {
    throw std::runtime_error("Invalid frame index");
  }
  
  return resources.descriptorPools[frameIndex];
}

uint32_t DescriptorPoolManager::get_max_frames_in_flight() const {
  return maxFramesInFlight;
}
