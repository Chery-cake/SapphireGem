#include "buffer.h"
#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <cstddef>
#include <future>
#include <memory>
#include <print>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

Buffer::Buffer(std::vector<LogicalDevice *> logicalDevices,
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
                         initialData = createInfo.initialData, promise]() {
      try {
        promise->set_value(create_buffer(device, resources, initialData));
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

Buffer::~Buffer() {

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

bool Buffer::create_buffer(LogicalDevice *device, BufferResources &resources,
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

void Buffer::destroy_buffer(LogicalDevice *device, BufferResources &resources) {
  if (resources.buffer != VK_NULL_HANDLE) {
    VmaAllocator allocator = device->get_allocator();
    vmaDestroyBuffer(allocator, resources.buffer, resources.allocation);
    resources.buffer = VK_NULL_HANDLE;
    resources.allocation = VK_NULL_HANDLE;
    resources.mappedData = nullptr;
  }
}

vk::BufferUsageFlags Buffer::get_buffer_usage_flags(BufferType type) {
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

VmaMemoryUsage Buffer::get_memory_usage(BufferUsage usage) {
  switch (usage) {
  case BufferUsage::STATIC:
    return VMA_MEMORY_USAGE_GPU_ONLY;
  case BufferUsage::DYNAMIC:
    return VMA_MEMORY_USAGE_CPU_TO_GPU;
  default:
    return VMA_MEMORY_USAGE_AUTO;
  }
}

VmaAllocationCreateFlags Buffer::get_allocation_flags(BufferUsage usage) {
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

bool Buffer::update_data(const void *data, vk::DeviceSize dataSize,
                         vk::DeviceSize offset) {
  if (usage != BufferUsage::DYNAMIC) {
    std::print("Cannot update static buffer {}\n", identifier);
    return false;
  }

  if (offset + dataSize > size) {
    std::print("Data exceeds buffer size for {}\n", identifier);
    return false;
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

void Buffer::bind(vk::raii::CommandBuffer &commandBuffer,
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

void Buffer::bind_vertex(vk::raii::CommandBuffer &commandBuffer,
                         uint32_t binding, vk::DeviceSize offset,
                         uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    return;
  }

  VkBuffer buffer = deviceResources[deviceIndex]->buffer;
  commandBuffer.bindVertexBuffers(binding, {buffer}, {offset});
}

void Buffer::bind_index(vk::raii::CommandBuffer &commandBuffer,
                        vk::IndexType indexType, vk::DeviceSize offset,
                        uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    return;
  }

  VkBuffer buffer = deviceResources[deviceIndex]->buffer;
  commandBuffer.bindIndexBuffer(buffer, offset, indexType);
}

VkBuffer Buffer::get_buffer(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return VK_NULL_HANDLE;
  }
  return deviceResources[deviceIndex]->buffer;
}

vk::DeviceSize Buffer::get_size() const { return size; }

Buffer::BufferType Buffer::get_type() const { return type; }

Buffer::BufferUsage Buffer::get_usage() const { return usage; }

const std::string &Buffer::get_identifier() const { return identifier; }

bool Buffer::is_mapped(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return false;
  }
  return deviceResources[deviceIndex]->mappedData != nullptr;
}

void *Buffer::get_mapped_data(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    return nullptr;
  }
  return deviceResources[deviceIndex]->mappedData;
}
