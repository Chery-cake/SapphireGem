#include "buffer.h"
#include "config.h"
#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <cstddef>
#include <future>
#include <memory>
#include <print>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

device::Buffer::Buffer(std::vector<LogicalDevice *> logicalDevices,
                       const BufferCreateInfo &createInfo)
    : identifier(createInfo.identifier), type(createInfo.type),
      usage(createInfo.usage), logicalDevices(logicalDevices),
      size(createInfo.size), elementSize(createInfo.elementSize) {

  deviceResources.reserve(logicalDevices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<BufferResources>());
  }

  std::vector<std::future<bool>> futures;

  for (size_t i = 0; i < logicalDevices.size(); i++) {
    auto *device = logicalDevices[i];
    auto &resources = *deviceResources[i];

    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    device->submit_task([this, device, &resources,
                         initialData = createInfo.initialData,
                         createDescriptorSets = createInfo.createDescriptorSets,
                         promise]() {
      try {
        bool success = create_buffer(device, resources, initialData);
        if (success && createDescriptorSets) {
          success = create_descriptor_sets_for_buffer(device, resources);
        }
        promise->set_value(success);
      } catch (const std::exception &e) {
        std::print(
            "Failed to create buffer on device {}: {}\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
        promise->set_value(false);
      }
    });
  }
  bool allSuccess = true;
  for (size_t i = 0; i < futures.size(); ++i) {
    if (!futures[i].get()) {
      std::print("Failed to initialize buffer {} on device {}\n", identifier,
                 logicalDevices[i]
                     ->get_physical_device()
                     ->get_properties()
                     .deviceName.data());
      allSuccess = false;
    }
  }

  if (allSuccess) {
    std::print("Buffer - {} - initialized successfully on {} devices\n",
               identifier, logicalDevices.size());
  }
}

device::Buffer::~Buffer() {

  std::lock_guard lock(bufferMutex);

  std::vector<std::future<void>> futures;

  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    auto *device = logicalDevices[i];
    auto &resources = *deviceResources[i];

    auto promise = std::make_shared<std::promise<void>>();
    futures.push_back(promise->get_future());

    device->submit_task([this, device, &resources, promise]() {
      destroy_buffer(device, resources);
      promise->set_value();
    });
  }

  for (auto &future : futures) {
    future.wait();
  }

  deviceResources.clear();

  std::print("Buffer - {} - destructor executed\n", identifier);
}

bool device::Buffer::create_buffer(LogicalDevice *device,
                                   BufferResources &resources,
                                   const void *initialData) {
  VmaAllocator allocator = device->get_allocator();

  VkBufferCreateInfo bufferInfo = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = static_cast<VkBufferUsageFlags>(get_buffer_usage_flags(type)),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

  VmaAllocationCreateInfo allocInfo = {.flags = get_allocation_flags(usage),
                                       .usage = get_memory_usage(usage)};

  VkResult result =
      vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &resources.buffer,
                      &resources.allocation, &resources.allocationInfo);

  if (result != VK_SUCCESS) {
    std::print(
        "Failed to create buffer - {} - on device {}\n",
        static_cast<int>(result),
        device->get_physical_device()->get_properties().deviceName.data());
    return false;
  }

  if (usage == BufferUsage::DYNAMIC) {
    resources.mappedData = resources.allocationInfo.pMappedData;
  }

  if (initialData != nullptr) {
    if (usage == BufferUsage::DYNAMIC || usage == BufferUsage::STREAMING) {
      std::memcpy(resources.mappedData, initialData, size);
    } else {
      VkBufferCreateInfo stagingInfo = {
          .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
          .size = size,
          .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
          .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

      VmaAllocationCreateInfo stagingAllocInfo = {
          .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT,
          .usage = VMA_MEMORY_USAGE_CPU_ONLY};

      VkBuffer stagingBuffer;
      VmaAllocation stagingAllocation;
      VmaAllocationInfo stagingAllocationInfo;

      result = vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo,
                               &stagingBuffer, &stagingAllocation,
                               &stagingAllocationInfo);

      if (result != VK_SUCCESS) {
        std::print(
            "Failed to create staging buffer - {} - on device {}\n",
            static_cast<int>(result),
            device->get_physical_device()->get_properties().deviceName.data());
        return false;
      }

      std::memcpy(stagingAllocationInfo.pMappedData, initialData, size);

      vk::CommandPoolCreateInfo poolInfo{
          .flags = vk::CommandPoolCreateFlagBits::eTransient,
          .queueFamilyIndex = device->get_graphics_queue_index()};

      auto commandPool = device->get_device().createCommandPool(poolInfo);

      vk::CommandBufferAllocateInfo allocCmdInfo{
          .commandPool = *commandPool,
          .level = vk::CommandBufferLevel::ePrimary,
          .commandBufferCount = 1};

      auto commandBuffers =
          device->get_device().allocateCommandBuffers(allocCmdInfo);
      auto &cmd = commandBuffers[0];

      vk::CommandBufferBeginInfo beginInfo{
          .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};

      cmd.begin(beginInfo);

      vk::BufferCopy copyRegion{.srcOffset = 0, .dstOffset = 0, .size = size};

      cmd.copyBuffer(stagingBuffer, resources.buffer, copyRegion);
      cmd.end();

      vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                                .pCommandBuffers = &*cmd};

      device->get_graphics_queue().submit(submitInfo);
      device->get_graphics_queue().waitIdle();

      vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    }
  }

  return true;
}

void device::Buffer::destroy_buffer(LogicalDevice *device,
                                    BufferResources &resources) {
  if (resources.buffer != VK_NULL_HANDLE) {
    VmaAllocator allocator = device->get_allocator();
    vmaDestroyBuffer(allocator, resources.buffer, resources.allocation);
    resources.buffer = VK_NULL_HANDLE;
    resources.allocation = VK_NULL_HANDLE;
    resources.mappedData = nullptr;
  }
}

bool device::Buffer::create_descriptor_sets_for_buffer(
    LogicalDevice *device, BufferResources &resources) {
  try {
    // Only create descriptor sets for uniform and storage buffers
    if (type != BufferType::UNIFORM && type != BufferType::STORAGE) {
      return true; // Not an error, just not needed
    }

    uint32_t maxFrames = general::Config::get_instance().get_max_frames();

    // Create a simple descriptor set layout for this buffer
    vk::DescriptorType descriptorType =
        (type == BufferType::UNIFORM) ? vk::DescriptorType::eUniformBuffer
                                      : vk::DescriptorType::eStorageBuffer;

    vk::DescriptorSetLayoutBinding binding{
        .binding = 0,
        .descriptorType = descriptorType,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex |
                      vk::ShaderStageFlagBits::eFragment};

    vk::DescriptorSetLayoutCreateInfo layoutInfo{.bindingCount = 1,
                                                 .pBindings = &binding};

    resources.descriptorSetLayout =
        device->get_device().createDescriptorSetLayout(layoutInfo);

    // Allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(
        maxFrames, *resources.descriptorSetLayout);

    vk::DescriptorSetAllocateInfo allocInfo{.descriptorPool =
                                                *device->get_descriptor_pool(),
                                            .descriptorSetCount = maxFrames,
                                            .pSetLayouts = layouts.data()};

    resources.descriptorSets =
        vk::raii::DescriptorSets(device->get_device(), allocInfo);

    // Update descriptor sets to point to this buffer
    for (uint32_t i = 0; i < maxFrames; ++i) {
      vk::DescriptorBufferInfo bufferInfo{
          .buffer = resources.buffer, .offset = 0, .range = size};

      vk::WriteDescriptorSet descriptorWrite{.dstSet =
                                                 *resources.descriptorSets[i],
                                             .dstBinding = 0,
                                             .dstArrayElement = 0,
                                             .descriptorCount = 1,
                                             .descriptorType = descriptorType,
                                             .pBufferInfo = &bufferInfo};

      device->get_device().updateDescriptorSets(descriptorWrite, nullptr);
    }

    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create descriptor sets for buffer {}: {}\n",
               identifier, e.what());
    return false;
  }
}

vk::BufferUsageFlags device::Buffer::get_buffer_usage_flags(BufferType type) {
  switch (type) {
  case BufferType::VERTEX:
    return vk::BufferUsageFlagBits::eVertexBuffer |
           vk::BufferUsageFlagBits::eTransferDst;
    break;
  case BufferType::INDEX:
    return vk::BufferUsageFlagBits::eIndexBuffer |
           vk::BufferUsageFlagBits::eTransferDst;
    break;
  case BufferType::UNIFORM:
    return vk::BufferUsageFlagBits::eUniformBuffer;
    break;
  case BufferType::STORAGE:
    return vk::BufferUsageFlagBits::eStorageBuffer;
    break;
  case BufferType::STAGING:
    return vk::BufferUsageFlagBits::eTransferSrc;
    break;
  default:
    return vk::BufferUsageFlagBits::eVertexBuffer;
  }
}

VmaMemoryUsage device::Buffer::get_memory_usage(BufferUsage usage) {
  switch (usage) {
  case BufferUsage::STATIC:
    return VMA_MEMORY_USAGE_GPU_ONLY;
  case BufferUsage::DYNAMIC:
    return VMA_MEMORY_USAGE_CPU_TO_GPU;
  default:
    return VMA_MEMORY_USAGE_AUTO;
  }
}

VmaAllocationCreateFlags
device::Buffer::get_allocation_flags(BufferUsage usage) {
  switch (usage) {
  case BufferUsage::STREAMING:
  case BufferUsage::DYNAMIC:
    return VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
           VMA_ALLOCATION_CREATE_MAPPED_BIT;
  case BufferUsage::STATIC:
  default:
    return 0;
  }
}

bool device::Buffer::update_data(const void *data, vk::DeviceSize dataSize,
                                 vk::DeviceSize offset) {
  if (usage != BufferUsage::DYNAMIC) {
    std::print("Cannot update static buffer {}\n", identifier);
    return false;
  }

  if (offset + dataSize > size) {
    std::print("Data exceeds buffer size for {}\n", identifier);
    return false;
  }

  // Check for null data pointer
  if (data == nullptr || dataSize == 0) {
    return true; // Nothing to update, but not an error
  }

  std::lock_guard lock(bufferMutex);

  for (size_t i = 0; i < deviceResources.size(); ++i) {
    auto &resources = *deviceResources[i];
    if (resources.mappedData != nullptr) {
      std::memcpy(static_cast<char *>(resources.mappedData) + offset, data,
                  dataSize);
    }
  }

  return true;
}

void device::Buffer::bind(vk::raii::CommandBuffer &commandBuffer,
                          uint32_t deviceIndex) {
  switch (type) {
  case BufferType::INDEX:
    bind_index(commandBuffer, vk::IndexType::eUint32, 0, deviceIndex);
    break;
  case BufferType::VERTEX:
    bind_vertex(commandBuffer, 0, 0, deviceIndex);
    break;
  default:
    break;
  }
}

void device::Buffer::bind_vertex(vk::raii::CommandBuffer &commandBuffer,
                                 uint32_t binding, vk::DeviceSize offset,
                                 uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    return;
  }

  VkBuffer buffer = deviceResources[deviceIndex]->buffer;
  commandBuffer.bindVertexBuffers(binding, {buffer}, {offset});
}

void device::Buffer::bind_index(vk::raii::CommandBuffer &commandBuffer,
                                vk::IndexType indexType, vk::DeviceSize offset,
                                uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    return;
  }

  VkBuffer buffer = deviceResources[deviceIndex]->buffer;
  commandBuffer.bindIndexBuffer(buffer, offset, indexType);
}

VkBuffer device::Buffer::get_buffer(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return VK_NULL_HANDLE;
  }
  return deviceResources[deviceIndex]->buffer;
}

vk::DeviceSize device::Buffer::get_size() const { return size; }

device::Buffer::BufferType device::Buffer::get_type() const { return type; }

device::Buffer::BufferUsage device::Buffer::get_usage() const { return usage; }

const std::string &device::Buffer::get_identifier() const { return identifier; }

bool device::Buffer::is_mapped(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return false;
  }
  return deviceResources[deviceIndex]->mappedData != nullptr;
}

void *device::Buffer::get_mapped_data(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return nullptr;
  }
  return deviceResources[deviceIndex]->mappedData;
}
