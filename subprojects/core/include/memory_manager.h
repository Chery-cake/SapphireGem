#ifndef MEMORY_MANAGER_H_
#define MEMORY_MANAGER_H_

#include "bump_allocator.h"
#include <memory>

namespace core {

// Manages memory allocators for the engine
class CORE_API MemoryManager {
public:
  static MemoryManager &instance();

  // Initialize the memory manager with allocator sizes
  void initialize(size_t persistentSize = 10 * 1024 * 1024, // 10MB
                  size_t frameSize = 5 * 1024 * 1024);      // 5MB

  // Get allocators
  BumpAllocator &getPersistentAllocator();
  BumpAllocator &getFrameAllocator();

  // Reset frame allocator (call at start of each frame)
  void resetFrameAllocator();

  // Get allocation statistics
  size_t getPersistentBytesAllocated() const;
  size_t getFrameBytesAllocated() const;

  void shutdown();

  // Disable copying
  MemoryManager(const MemoryManager &) = delete;
  MemoryManager &operator=(const MemoryManager &) = delete;

private:
  MemoryManager() = default;
  ~MemoryManager() = default;

  std::unique_ptr<BumpAllocator> persistentAlloc;
  std::unique_ptr<BumpAllocator> frameAlloc;
};

} // namespace core

#endif // MEMORY_MANAGER_H_
