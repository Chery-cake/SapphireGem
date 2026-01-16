#include "memory_manager.h"
#include <stdexcept>

namespace core {

MemoryManager &MemoryManager::instance() {
  static MemoryManager instance;
  return instance;
}

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
