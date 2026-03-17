#include "memory_allocator.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <new>
#include <print>

// ============================================================================
// MemoryAllocator Implementation
// ============================================================================

MemoryAllocator::MemoryAllocator(size_t size) : total_size_(size) {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  memory_ = static_cast<uint8_t *>(::operator new(size));
}

MemoryAllocator::~MemoryAllocator() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  if (memory_ != nullptr) {
    ::operator delete(memory_);
  }
}

void *MemoryAllocator::allocate(size_t size, size_t alignment) {
  std::println(stderr,
               "[MemoryAllocator] Tried allocating {} bytes with alignment {}",
               size, alignment);
  return nullptr;
}

void *MemoryAllocator::push() {
  std::println(stderr, "[MemoryAllocator] Tried pushing new memory");
  return nullptr;
}

void *MemoryAllocator::pop() {
  std::println(stderr, "[MemoryAllocator] Tried to pop the last memory");
  return nullptr;
}

void MemoryAllocator::reset() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  ::operator delete(memory_);
  memory_ = static_cast<uint8_t *>(::operator new(total_size_));
  std::println(stderr, "[MemoryAllocator] Memory cleared and recreated without "
                       "resetting the offsets/counts");
}

// ============================================================================
// BumpAllocator Implementation
// ============================================================================

BumpAllocator::BumpAllocator(size_t size) : MemoryAllocator(size) {}

BumpAllocator::~BumpAllocator() {}

void *BumpAllocator::allocate(size_t size, size_t alignment) {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  // Align current offset
  size_t padding = 0;
  size_t current = reinterpret_cast<uintptr_t>(memory() + offset_);
  size_t aligned = (current + alignment - 1) & ~(alignment - 1);
  padding = aligned - current;

  if (offset_ + padding + size > capacity()) {
    std::print(stderr,
               "[BumpAllocator] Out of memory! Requested: {}, Available: {}\n",
               size, capacity() - offset_);
    return nullptr;
  }

  offset_ += padding;
  void *result = static_cast<void *>(const_cast<uint8_t *>(memory()) + offset_);
  offset_ += size;

  return result;
}

void BumpAllocator::reset() {
  std::lock_guard<std::mutex> lock(memoryMutex_);
  offset_ = 0;
}

// ============================================================================
// StockAllocator Implementation
// ============================================================================

// ============================================================================
// PoolAllocator Implementation
// ============================================================================
