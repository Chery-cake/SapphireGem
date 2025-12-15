#include "buffer_manager.h"
#include "device_manager.h"
#include <print>

device::BufferManager::BufferManager(const DeviceManager *deviceManager)
    : deviceManager(deviceManager) {}

device::BufferManager::~BufferManager() {
  std::lock_guard lock(managerMutex);
  buffers.clear();
  std::print("Buffer Manager destructor executed\n");
}

device::Buffer *device::BufferManager::create_buffer(
    const Buffer::BufferCreateInfo &createInfo) {
  std::lock_guard lock(managerMutex);

  if (buffers.find(createInfo.identifier) != buffers.end()) {
    std::print("Buffer with name {} already exists\n", createInfo.identifier);
    return buffers[createInfo.identifier].get();
  }

  auto buffer = std::make_unique<Buffer>(
      deviceManager->get_all_logical_devices(), createInfo);

  Buffer *bufferPtr = buffer.get();
  buffers[createInfo.identifier] = std::move(buffer);

  return bufferPtr;
}

device::Buffer *device::BufferManager::create_vertex_buffer(
    const std::string &identifier, vk::DeviceSize size, const void *data,
    Buffer::BufferUsage usage) {
  Buffer::BufferCreateInfo createInfo{.identifier = identifier,
                                      .type = Buffer::BufferType::VERTEX,
                                      .usage = usage,
                                      .size = size,
                                      .initialData = data};

  return create_buffer(createInfo);
}

device::Buffer *device::BufferManager::create_index_buffer(
    const std::string &identifier, vk::DeviceSize size, const void *data,
    Buffer::BufferUsage usage) {
  Buffer::BufferCreateInfo createInfo{.identifier = identifier,
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = usage,
                                      .size = size,
                                      .initialData = data};

  return create_buffer(createInfo);
}

device::Buffer *device::BufferManager::create_uniform_buffer(
    const std::string &identifier, vk::DeviceSize size, const void *data,
    Buffer::BufferUsage usage) {
  Buffer::BufferCreateInfo createInfo{.identifier = identifier,
                                      .type = Buffer::BufferType::UNIFORM,
                                      .usage = usage,
                                      .size = size,
                                      .initialData = data};

  return create_buffer(createInfo);
}

void device::BufferManager::remove_buffer(const std::string &identifier) {
  std::lock_guard lock(managerMutex);

  auto it = buffers.find(identifier);
  if (it != buffers.end()) {
    buffers.erase(it);
    std::print("Removed buffer: {}\n", identifier);
  }
}

device::Buffer *
device::BufferManager::get_buffer(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);

  auto it = buffers.find(identifier);
  if (it != buffers.end()) {
    return it->second.get();
  }
  return nullptr;
}

bool device::BufferManager::has_buffer(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);
  return buffers.find(identifier) != buffers.end();
}

std::vector<device::Buffer *> device::BufferManager::get_all_buffers() const {
  std::lock_guard lock(managerMutex);

  std::vector<Buffer *> result;
  result.reserve(buffers.size());

  for (const auto &[key, buffer] : buffers) {
    result.push_back(buffer.get());
  }

  return result;
}

std::vector<device::Buffer *>
device::BufferManager::get_buffers_by_type(Buffer::BufferType type) const {
  std::lock_guard lock(managerMutex);

  std::vector<Buffer *> result;

  for (const auto &[key, buffer] : buffers) {
    if (buffer->get_type() == type) {
      result.push_back(buffer.get());
    }
  }

  return result;
}
