#ifndef BUMP_ALLOCATOR_H_
#define BUMP_ALLOCATOR_H_

#include "core_export.h"
#include <cstddef>

// Simple Bump Allocator for fast allocations
class CORE_API BumpAllocator {
public:
  explicit BumpAllocator(size_t size);
  ~BumpAllocator();

  void *allocate(size_t size, size_t alignment = alignof(std::max_align_t));
  void reset();
  size_t bytes_allocated() const { return offset; }
  size_t capacity() const { return total_size; }

  // Disable copying
  BumpAllocator(const BumpAllocator &) = delete;
  BumpAllocator &operator=(const BumpAllocator &) = delete;

private:
  char *memory;
  size_t total_size;
  size_t offset;
};

// RAII helper for scoped allocations
template <typename T> class ScopedAlloc {
public:
  ScopedAlloc(BumpAllocator &alloc, size_t count = 1)
      : allocator(alloc), ptr(nullptr),
        initial_offset(alloc.bytes_allocated()) {
    ptr = static_cast<T *>(alloc.allocate(sizeof(T) * count, alignof(T)));
  }

  T *get() { return ptr; }
  T &operator*() { return *ptr; }
  T *operator->() { return ptr; }
  operator T *() { return ptr; }

private:
  BumpAllocator &allocator;
  T *ptr;
  size_t initial_offset;
};

#endif // BUMP_ALLOCATOR_H_
