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
  if (persistentAlloc || frameAlloc) {
    return; // Already initialized
  }

  persistentAlloc = std::make_unique<BumpAllocator>(persistentSize);
  frameAlloc = std::make_unique<BumpAllocator>(frameSize);
}

BumpAllocator &MemoryManager::getPersistentAllocator() {
  if (!persistentAlloc) {
    throw std::runtime_error(
        "MemoryManager not initialized. Call initialize() first.");
  }
  return *persistentAlloc;
}

BumpAllocator &MemoryManager::getFrameAllocator() {
  if (!frameAlloc) {
    throw std::runtime_error(
        "MemoryManager not initialized. Call initialize() first.");
  }
  return *frameAlloc;
}

void MemoryManager::resetFrameAllocator() {
  if (frameAlloc) {
    frameAlloc->reset();
  }
}

size_t MemoryManager::getPersistentBytesAllocated() const {
  return persistentAlloc ? persistentAlloc->bytes_allocated() : 0;
}

size_t MemoryManager::getFrameBytesAllocated() const {
  return frameAlloc ? frameAlloc->bytes_allocated() : 0;
}

void MemoryManager::shutdown() {
  persistentAlloc.reset();
  frameAlloc.reset();
}

} // namespace core
