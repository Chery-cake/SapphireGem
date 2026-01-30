#include "memory_manager.h"
#include <mutex>
#include <stdexcept>

namespace core {

#ifdef ENGINE_DEBUG
static MemoryManager *g_memoryManagerInstance = nullptr;
#endif

MemoryManager &MemoryManager::instance() {
#ifdef ENGINE_DEBUG
  if (g_memoryManagerInstance) {
    return *g_memoryManagerInstance;
  }
#endif
  static MemoryManager instance;
#ifdef ENGINE_DEBUG
  g_memoryManagerInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void MemoryManager::setInstance(MemoryManager *inst) {
  g_memoryManagerInstance = inst;
}

MemoryManager *MemoryManager::getInstance() { return g_memoryManagerInstance; }
#endif

BumpAllocator &MemoryManager::createPersistentAllocator(const std::string &name,
                                                        size_t size) {
  if (size == 0) {
    throw std::runtime_error("Allocator size must be greater than 0");
  }

  std::lock_guard<std::mutex> lock(allocatorMutex_);

  if (persistentAllocators_.find(name) != persistentAllocators_.end()) {
    throw std::runtime_error("Persistent allocator '" + name +
                             "' already exists");
  }

  persistentAllocators_[name] = std::make_unique<BumpAllocator>(size);
  return *persistentAllocators_[name];
}

BumpAllocator &MemoryManager::createFrameAllocator(const std::string &name,
                                                   size_t size) {
  if (size == 0) {
    throw std::runtime_error("Allocator size must be greater than 0");
  }

  std::lock_guard<std::mutex> lock(allocatorMutex_);

  if (frameAllocators_.find(name) != frameAllocators_.end()) {
    throw std::runtime_error("Frame allocator '" + name + "' already exists");
  }

  frameAllocators_[name] = std::make_unique<BumpAllocator>(size);
  return *frameAllocators_[name];
}

BumpAllocator &MemoryManager::getPersistentAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = persistentAllocators_.find(name);
  if (it == persistentAllocators_.end()) {
    throw std::runtime_error(
        "Persistent allocator '" + name +
        "' not found. Call createPersistentAllocator() first.");
  }
  return *it->second;
}

BumpAllocator &MemoryManager::getFrameAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = frameAllocators_.find(name);
  if (it == frameAllocators_.end()) {
    throw std::runtime_error("Frame allocator '" + name +
                             "' not found. Call createFrameAllocator() first.");
  }
  return *it->second;
}

bool MemoryManager::hasPersistentAllocator(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);
  return persistentAllocators_.find(name) != persistentAllocators_.end();
}

bool MemoryManager::hasFrameAllocator(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);
  return frameAllocators_.find(name) != frameAllocators_.end();
}

bool MemoryManager::destroyPersistentAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = persistentAllocators_.find(name);
  if (it == persistentAllocators_.end()) {
    return false;
  }

  persistentAllocators_.erase(it);
  return true;
}

bool MemoryManager::destroyFrameAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = frameAllocators_.find(name);
  if (it == frameAllocators_.end()) {
    return false;
  }

  frameAllocators_.erase(it);
  return true;
}

void MemoryManager::resetFrameAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = frameAllocators_.find(name);
  if (it != frameAllocators_.end()) {
    it->second->reset();
  }
}

void MemoryManager::resetAllFrameAllocators() {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  for (auto &pair : frameAllocators_) {
    pair.second->reset();
  }
}

size_t
MemoryManager::getPersistentBytesAllocated(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = persistentAllocators_.find(name);
  return it != persistentAllocators_.end() ? it->second->bytes_allocated() : 0;
}

size_t MemoryManager::getFrameBytesAllocated(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  auto it = frameAllocators_.find(name);
  return it != frameAllocators_.end() ? it->second->bytes_allocated() : 0;
}

std::vector<std::string> MemoryManager::getPersistentAllocatorNames() const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  std::vector<std::string> names;
  names.reserve(persistentAllocators_.size());
  for (const auto &pair : persistentAllocators_) {
    names.push_back(pair.first);
  }
  return names;
}

std::vector<std::string> MemoryManager::getFrameAllocatorNames() const {
  std::lock_guard<std::mutex> lock(allocatorMutex_);

  std::vector<std::string> names;
  names.reserve(frameAllocators_.size());
  for (const auto &pair : frameAllocators_) {
    names.push_back(pair.first);
  }
  return names;
}

void MemoryManager::shutdown() {
  std::lock_guard<std::mutex> lock(allocatorMutex_);
  persistentAllocators_.clear();
  frameAllocators_.clear();
}

} // namespace core
