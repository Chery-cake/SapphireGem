#pragma once

#include "logical_device.h"
#include <glm/detail/qualifier.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/mat4x4.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

class Buffer {
public:
  enum class BufferType { VERTEX, INDEX, UNIFORM, STORAGE, STAGING };

  enum class BufferUsage {
    STATIC,   // One-time upload, GPU-only
    DYNAMIC,  // Frequent updates, CPU-to-GPU
    STREAMING // Constant updates, CPU-to-GPU with ring buffer
  };

  struct BufferCreateInfo {
    std::string identifier;
    BufferType type;
    BufferUsage usage;
    VkDeviceSize size;
    VkDeviceSize elementSize = 0;
    VkBufferUsageFlags vkUsage;
    VmaMemoryUsage memoryUsage;
    VmaAllocationCreateFlags allocationFlags = 0;
    const void *initialData;
  };

  struct BufferResources {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;
    void *mappedData;
    bool isMapped;
    bool isPersistentlyMapped;

    BufferResources()
        : buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE),
          mappedData(nullptr), isMapped(false), isPersistentlyMapped(false) {}
  };

private:
  mutable std::mutex bufferMutex;

  std::string identifier;
  BufferType type;
  BufferUsage usage;

  std::vector<LogicalDevice *> logicalDevices;
  std::vector<std::unique_ptr<BufferResources>> deviceResources;

  VkDeviceSize size;
  VkDeviceSize elementSize;

  bool create_buffer(LogicalDevice *device, BufferResources &resources,
                     const void *initialData);
  void destroy_buffer(LogicalDevice *device, BufferResources &resources);

  static vk::BufferUsageFlags get_buffer_usage_flags(BufferType type);
  static VmaMemoryUsage get_memory_usage(BufferUsage usage);
  static VmaAllocationCreateFlags get_allocation_flags(BufferUsage usage);

public:
  Buffer(std::vector<LogicalDevice *> logicalDevices,
         const BufferCreateInfo &createInfo);
  ~Buffer();

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&) = delete;
  Buffer &operator=(Buffer &&) = delete;

  bool update_data(const void *data, vk::DeviceSize dataSize,
                   vk::DeviceSize offset = 0);

  void bind(vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex = 0);
  void bind_vertex(vk::raii::CommandBuffer &commandBuffer, uint32_t binding = 0,
                   vk::DeviceSize offset = 0, uint32_t deviceIndex = 0);
  void bind_index(vk::raii::CommandBuffer &commandBuffer,
                  vk::IndexType indexType = vk::IndexType::eUint32,
                  vk::DeviceSize offset = 0, uint32_t deviceIndex = 0);

  VkBuffer get_buffer(uint32_t deviceIndex = 0) const;
  vk::DeviceSize get_size() const;
  BufferType get_type() const;
  BufferUsage get_usage() const;
  const std::string &get_identifier() const;
  bool is_mapped(uint32_t deviceIndex = 0) const;
  void *get_mapped_data(uint32_t deviceIndex = 0) const;
};
