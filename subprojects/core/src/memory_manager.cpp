#include "memory_manager.h"
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

void MemoryManager::initialize(size_t persistentSize, size_t frameSize) {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  // Check if default allocators already exist
  if (persistentAllocators.find(DEFAULT_ALLOCATOR_NAME) !=
          persistentAllocators.end() ||
      frameAllocators.find(DEFAULT_ALLOCATOR_NAME) != frameAllocators.end()) {
    return; // Already initialized
  }

  persistentAllocators[DEFAULT_ALLOCATOR_NAME] =
      std::make_unique<BumpAllocator>(persistentSize);
  frameAllocators[DEFAULT_ALLOCATOR_NAME] =
      std::make_unique<BumpAllocator>(frameSize);
}

// ========== Default Allocator Access (Backward Compatible) ==========

BumpAllocator &MemoryManager::getPersistentAllocator() {
  return getPersistentAllocator(DEFAULT_ALLOCATOR_NAME);
}

BumpAllocator &MemoryManager::getFrameAllocator() {
  return getFrameAllocator(DEFAULT_ALLOCATOR_NAME);
}

void MemoryManager::resetFrameAllocator() {
  resetFrameAllocator(DEFAULT_ALLOCATOR_NAME);
}

size_t MemoryManager::getPersistentBytesAllocated() const {
  return getPersistentBytesAllocated(DEFAULT_ALLOCATOR_NAME);
}

size_t MemoryManager::getFrameBytesAllocated() const {
  return getFrameBytesAllocated(DEFAULT_ALLOCATOR_NAME);
}

// ========== Named Allocator Management ==========

BumpAllocator &MemoryManager::createPersistentAllocator(const std::string &name,
                                                        size_t size) {
  if (size == 0) {
    throw std::runtime_error("Allocator size must be greater than 0");
  }

  std::lock_guard<std::mutex> lock(allocatorMutex);

  if (persistentAllocators.find(name) != persistentAllocators.end()) {
    throw std::runtime_error("Persistent allocator '" + name +
                             "' already exists");
  }

  persistentAllocators[name] = std::make_unique<BumpAllocator>(size);
  return *persistentAllocators[name];
}

BumpAllocator &MemoryManager::createFrameAllocator(const std::string &name,
                                                   size_t size) {
  if (size == 0) {
    throw std::runtime_error("Allocator size must be greater than 0");
  }

  std::lock_guard<std::mutex> lock(allocatorMutex);

  if (frameAllocators.find(name) != frameAllocators.end()) {
    throw std::runtime_error("Frame allocator '" + name + "' already exists");
  }

  frameAllocators[name] = std::make_unique<BumpAllocator>(size);
  return *frameAllocators[name];
}

BumpAllocator &
MemoryManager::getPersistentAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = persistentAllocators.find(name);
  if (it == persistentAllocators.end()) {
    throw std::runtime_error("Persistent allocator '" + name +
                             "' not found. Call createPersistentAllocator() or "
                             "initialize() first.");
  }
  return *it->second;
}

BumpAllocator &MemoryManager::getFrameAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = frameAllocators.find(name);
  if (it == frameAllocators.end()) {
    throw std::runtime_error(
        "Frame allocator '" + name +
        "' not found. Call createFrameAllocator() or initialize() first.");
  }
  return *it->second;
}

bool MemoryManager::hasPersistentAllocator(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex);
  return persistentAllocators.find(name) != persistentAllocators.end();
}

bool MemoryManager::hasFrameAllocator(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex);
  return frameAllocators.find(name) != frameAllocators.end();
}

bool MemoryManager::destroyPersistentAllocator(const std::string &name) {
  // Prevent destruction of the default allocator to maintain backward
  // compatibility
  if (name == DEFAULT_ALLOCATOR_NAME) {
    return false;
  }

  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = persistentAllocators.find(name);
  if (it == persistentAllocators.end()) {
    return false;
  }

  persistentAllocators.erase(it);
  return true;
}

bool MemoryManager::destroyFrameAllocator(const std::string &name) {
  // Prevent destruction of the default allocator to maintain backward
  // compatibility
  if (name == DEFAULT_ALLOCATOR_NAME) {
    return false;
  }

  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = frameAllocators.find(name);
  if (it == frameAllocators.end()) {
    return false;
  }

  frameAllocators.erase(it);
  return true;
}

void MemoryManager::resetFrameAllocator(const std::string &name) {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = frameAllocators.find(name);
  if (it != frameAllocators.end()) {
    it->second->reset();
  }
}

void MemoryManager::resetAllFrameAllocators() {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  for (auto &pair : frameAllocators) {
    pair.second->reset();
  }
}

size_t
MemoryManager::getPersistentBytesAllocated(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = persistentAllocators.find(name);
  return it != persistentAllocators.end() ? it->second->bytes_allocated() : 0;
}

size_t MemoryManager::getFrameBytesAllocated(const std::string &name) const {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  auto it = frameAllocators.find(name);
  return it != frameAllocators.end() ? it->second->bytes_allocated() : 0;
}

std::vector<std::string> MemoryManager::getPersistentAllocatorNames() const {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  std::vector<std::string> names;
  names.reserve(persistentAllocators.size());
  for (const auto &pair : persistentAllocators) {
    names.push_back(pair.first);
  }
  return names;
}

std::vector<std::string> MemoryManager::getFrameAllocatorNames() const {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  std::vector<std::string> names;
  names.reserve(frameAllocators.size());
  for (const auto &pair : frameAllocators) {
    names.push_back(pair.first);
  }
  return names;
}

void MemoryManager::shutdown() {
  std::lock_guard<std::mutex> lock(allocatorMutex);

  persistentAllocators.clear();
  frameAllocators.clear();
}

} // namespace core
