#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

class LogicalDevice;

enum class BufferType { Vertex, Index, Uniform };

enum class BufferUsageMode { Static, Dynamic };

class Buffer {
public:
  struct BufferCreateInfo {
    std::string identifier;
    BufferType type;
    BufferUsageMode usageMode;
    vk::DeviceSize size;
    const void *initialData;
  };

  struct DeviceBufferResources {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo allocationInfo;
    void *mappedData;

    DeviceBufferResources()
        : buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), mappedData(nullptr) {}
  };

private:
  mutable std::mutex bufferMutex;

  std::string identifier;
  BufferType type;
  BufferUsageMode usageMode;
  vk::DeviceSize size;

  std::vector<LogicalDevice *> logicalDevices;
  std::vector<std::unique_ptr<DeviceBufferResources>> deviceResources;

  bool create_buffer(LogicalDevice *device, DeviceBufferResources &resources,
                     const void *initialData);
  void destroy_buffer(LogicalDevice *device, DeviceBufferResources &resources);

  static vk::BufferUsageFlags get_buffer_usage_flags(BufferType type);
  static VmaMemoryUsage get_memory_usage(BufferUsageMode mode);
  static VmaAllocationCreateFlags get_allocation_flags(BufferUsageMode mode);

public:
  Buffer(const std::vector<LogicalDevice *> &devices,
         const BufferCreateInfo &createInfo);
  ~Buffer();

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;
  Buffer(Buffer &&) = delete;
  Buffer &operator=(Buffer &&) = delete;

  bool update_data(const void *data, vk::DeviceSize dataSize,
                   vk::DeviceSize offset = 0);

  void bind(vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex = 0);
  void bind_vertex(vk::raii::CommandBuffer &commandBuffer,
                   uint32_t binding = 0, vk::DeviceSize offset = 0,
                   uint32_t deviceIndex = 0);
  void bind_index(vk::raii::CommandBuffer &commandBuffer,
                  vk::IndexType indexType = vk::IndexType::eUint32,
                  vk::DeviceSize offset = 0, uint32_t deviceIndex = 0);

  VkBuffer get_buffer(uint32_t deviceIndex = 0) const;
  vk::DeviceSize get_size() const;
  BufferType get_type() const;
  BufferUsageMode get_usage_mode() const;
  const std::string &get_identifier() const;
  bool is_mapped(uint32_t deviceIndex = 0) const;
  void *get_mapped_data(uint32_t deviceIndex = 0) const;
};
