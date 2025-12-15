#pragma once

#include "buffer.h"
#include "device_manager.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace device {

class BufferManager {
private:
  const DeviceManager *deviceManager;

  mutable std::mutex managerMutex;
  std::unordered_map<std::string, std::unique_ptr<Buffer>> buffers;

public:
  BufferManager(const DeviceManager *deviceManager);
  ~BufferManager();

  BufferManager(const BufferManager &) = delete;
  BufferManager &operator=(const BufferManager &) = delete;
  BufferManager(BufferManager &&) = delete;
  BufferManager &operator=(BufferManager &&) = delete;

  Buffer *create_buffer(const Buffer::BufferCreateInfo &createInfo);

  Buffer *create_vertex_buffer(
      const std::string &identifier, vk::DeviceSize size,
      const void *data = nullptr,
      Buffer::BufferUsage usageMode = Buffer::BufferUsage::STATIC);

  Buffer *create_index_buffer(
      const std::string &identifier, vk::DeviceSize size,
      const void *data = nullptr,
      Buffer::BufferUsage usageMode = Buffer::BufferUsage::STATIC);

  Buffer *create_uniform_buffer(
      const std::string &identifier, vk::DeviceSize size,
      const void *data = nullptr,
      Buffer::BufferUsage usageMode = Buffer::BufferUsage::DYNAMIC);

  void remove_buffer(const std::string &identifier);

  Buffer *get_buffer(const std::string &identifier) const;
  bool has_buffer(const std::string &identifier) const;

  std::vector<Buffer *> get_all_buffers() const;
  std::vector<Buffer *> get_buffers_by_type(Buffer::BufferType type) const;
};

} // namespace device
