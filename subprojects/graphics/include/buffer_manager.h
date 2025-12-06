#pragma once

#include "buffer.h"
#include "devices_manager.h"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class BufferManager {
private:
  const DevicesManager *devicesManager;

  mutable std::mutex managerMutex;
  std::unordered_map<std::string, std::unique_ptr<Buffer>> buffers;

public:
  BufferManager(const DevicesManager *devicesManager);
  ~BufferManager();

  BufferManager(const BufferManager &) = delete;
  BufferManager &operator=(const BufferManager &) = delete;
  BufferManager(BufferManager &&) = delete;
  BufferManager &operator=(BufferManager &&) = delete;

  Buffer *create_buffer(const Buffer::BufferCreateInfo &createInfo);

  Buffer *create_vertex_buffer(const std::string &identifier,
                               vk::DeviceSize size, const void *data = nullptr,
                               BufferUsageMode usageMode = BufferUsageMode::Static);

  Buffer *create_index_buffer(const std::string &identifier,
                              vk::DeviceSize size, const void *data = nullptr,
                              BufferUsageMode usageMode = BufferUsageMode::Static);

  Buffer *create_uniform_buffer(const std::string &identifier,
                                vk::DeviceSize size, const void *data = nullptr,
                                BufferUsageMode usageMode = BufferUsageMode::Dynamic);

  void remove_buffer(const std::string &identifier);

  Buffer *get_buffer(const std::string &identifier) const;
  bool has_buffer(const std::string &identifier) const;

  std::vector<Buffer *> get_all_buffers() const;
  std::vector<Buffer *> get_buffers_by_type(BufferType type) const;
};
