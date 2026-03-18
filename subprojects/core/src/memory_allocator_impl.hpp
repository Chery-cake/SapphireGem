#include "memory_allocator.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <print>

namespace core {

// ============================================================================
// MemoryAllocator Implementation
// ============================================================================

template <size_t size> MemoryAllocator<size>::MemoryAllocator() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  memory_ = static_cast<uint8_t *>(::operator new(size));
}

template <size_t size> MemoryAllocator<size>::~MemoryAllocator() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  if (memory_ != nullptr) {
    ::operator delete(memory_);
  }
}

// ============================================================================
// BumpAllocator Implementation
// ============================================================================

template <size_t size>
BumpAllocator<size>::BumpAllocator() : MemoryAllocator<size>(), offset_(0) {}

template <size_t size> BumpAllocator<size>::~BumpAllocator() = default;

template <size_t size>
template <typename T>
void *BumpAllocator<size>::allocate(size_t alignment) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  size_t allocation = sizeof(T);
  // Align current offset
  size_t padding = 0;
  size_t current = reinterpret_cast<uintptr_t>(this->memory() + offset_);
  size_t aligned = (current + alignment - 1) & ~(alignment - 1);
  padding = aligned - current;

  if (offset_ + padding + allocation > this->capacity()) {
    std::println(stderr,
                 "[BumpAllocator] Out of memory! Requested: {}, Available: {}",
                 allocation, this->capacity() - offset_);
    return nullptr;
  }

  offset_ += padding;
  void *result =
      static_cast<void *>(const_cast<uint8_t *>(this->memory()) + offset_);
  offset_ += allocation;

  return result;
}

template <size_t size> void BumpAllocator<size>::reset() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  offset_ = 0;
}

// ============================================================================
// StockAllocator Implementation
// ============================================================================

template <size_t size>
StockAllocator<size>::StockAllocator() : MemoryAllocator<size>(), offset_(0) {}

template <size_t size> StockAllocator<size>::~StockAllocator() = default;

template <size_t size>
template <typename T>
void *StockAllocator<size>::push(size_t alignment) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);

  size_t header_size = sizeof(size_t);
  size_t start = offset_ + header_size;

  // Align current offset
  size_t padding = 0;
  size_t current = reinterpret_cast<uintptr_t>(this->memory() + start);
  size_t aligned = (current + alignment - 1) & ~(alignment - 1);
  padding = aligned - current;

  size_t totalSize = header_size + padding + sizeof(T);

  if (offset_ + totalSize > this->capacity()) {
    std::println(stderr,
                 "[StockAllocator] Out of memory! Requested: {}, Available: {}",
                 totalSize, this->capacity() - offset_);
    return nullptr;
  }

  offset_ += header_size + padding;
  size_t *header = reinterpret_cast<size_t *>(
      const_cast<uint8_t *>(this->memory()) + offset_ - header_size);
  *header = totalSize;
  void *result =
      static_cast<void *>(const_cast<uint8_t *>(this->memory()) + offset_);
  offset_ += totalSize;

  return result;
}

template <size_t size> void StockAllocator<size>::pop(void *ptr) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  size_t *header = reinterpret_cast<size_t *>(reinterpret_cast<uint8_t *>(ptr) -
                                              sizeof(size_t));
  offset_ -= *header;
}

// ============================================================================
// PoolAllocator Implementation
// ============================================================================

template <typename T, size_t poolSize>
PoolAllocator<T, poolSize>::PoolAllocator()
    : core::MemoryAllocator<sizeof(T) * poolSize>() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);

  // initialize freeList
  uint8_t *mem = const_cast<uint8_t *>(this->memory());
  for (size_t i = 0; i < poolSize - 1; i++) {
    reinterpret_cast<Node *>(mem + (i * sizeof(T)))->next =
        reinterpret_cast<Node *>(mem + ((i + 1) * sizeof(T)));
  }
  reinterpret_cast<Node *>(mem + ((poolSize - 1) * sizeof(T)))->next = nullptr;
  freeList = reinterpret_cast<Node *>(mem);
}

template <typename T, size_t poolSize>
PoolAllocator<T, poolSize>::~PoolAllocator() = default;

template <typename T, size_t poolSize>
T *PoolAllocator<T, poolSize>::allocate() {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  if (freeList == nullptr)
    throw std::bad_alloc;
  Node *node = freeList;
  freeList = freeList->next;
  return reinterpret_cast<T *>(node);
}

template <typename T, size_t poolSize>
void PoolAllocator<T, poolSize>::deallocate(T *ptr) {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  Node *node = reinterpret_cast<Node *>(ptr);
  node->next = freeList;
  freeList = node;
}

template <typename T, size_t poolSize>
size_t PoolAllocator<T, poolSize>::available() const {
  std::lock_guard<std::mutex> lock(this->memoryMutex_);
  size_t count = 0;
  Node *node = freeList;
  while (node) {
    count++;
    node = node->next;
  }
  return count;
}

} // namespace core
