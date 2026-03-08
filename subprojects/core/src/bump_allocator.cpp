#include "bump_allocator.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include "print_compat.h"

// ============================================================================
// BumpAllocator Implementation
// ============================================================================

BumpAllocator::BumpAllocator(size_t size) : total_size(size), offset(0) {
  memory = static_cast<char *>(::operator new(size));
  if (!memory) {
    throw std::bad_alloc();
  }
}

BumpAllocator::~BumpAllocator() {
  if (memory) {
    ::operator delete(memory);
  }
}

void *BumpAllocator::allocate(size_t size, size_t alignment) {
  // Align current offset
  size_t padding = 0;
  size_t current = reinterpret_cast<uintptr_t>(memory + offset);
  size_t aligned = (current + alignment - 1) & ~(alignment - 1);
  padding = aligned - current;

  if (offset + padding + size > total_size) {
    std::print(stderr,
               "[BumpAllocator] Out of memory! Requested: {}, Available: {}\n",
               size, total_size - offset);
    return nullptr;
  }

  offset += padding;
  void *result = memory + offset;
  offset += size;

  return result;
}

void BumpAllocator::reset() { offset = 0; }
