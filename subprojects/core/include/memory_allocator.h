#ifndef BUMP_ALLOCATOR_H_
#define BUMP_ALLOCATOR_H_

#include "core_export.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace core {

template <size_t size> class MemoryAllocator {
public:
  explicit MemoryAllocator();
  virtual ~MemoryAllocator();

  // Disable copy and move
  MemoryAllocator(const MemoryAllocator &) = delete;
  MemoryAllocator &operator=(const MemoryAllocator &) = delete;
  MemoryAllocator(MemoryAllocator &&) = delete;
  MemoryAllocator &operator=(MemoryAllocator &&) = delete;

  /* removed so that only the child have the implementation
     need to check if it isn't beter to have a dumy version
     so that thei isn't a problem if the wrong function is called

  virtual void *allocate(size_t size,
                         size_t alignment = alignof(std::max_align_t));

  virtual void *push();
  virtual void *pop();

  virtual void reset();
  */

  [[nodiscard]] size_t capacity() const { return size; }

protected:
  mutable std::mutex memoryMutex_;

  [[nodiscard]] const uint8_t *memory() const { return memory_; }

private:
  uint8_t *memory_ = nullptr;
};

// Bump Allocator for fast allocations
template <size_t size>
class CORE_API BumpAllocator : public MemoryAllocator<size> {
public:
  explicit BumpAllocator();
  ~BumpAllocator() override;

  template <typename T>
  void *allocate(size_t alignment = alignof(std::max_align_t));

  void reset();

  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

// Stock Allocator for variable allocations
template <size_t size>
class CORE_API StockAllocator : public MemoryAllocator<size> {
public:
  explicit StockAllocator();
  ~StockAllocator() override;

  template <typename T>
  void *push(size_t alignment = alignof(std::max_align_t));
  void pop(void *ptr);

  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

// Pool Allocator for multiple identical allocations
template <typename T, size_t poolSize>
class CORE_API PoolAllocator : public MemoryAllocator<sizeof(T) * poolSize> {
public:
  explicit PoolAllocator();
  ~PoolAllocator() override;

  T *allocate();
  void deallocate(T *ptr);

  size_t available() const;

private:
  union Node {
    alignas(T) uint8_t data[sizeof(T)];
    Node *next;
  };

  Node *freeList;
};

} // namespace core

// template implementation
#include "../src/memory_allocator_impl.hpp"

#endif // BUMP_ALLOCATOR_H_
