#ifndef BUMP_ALLOCATOR_H_
#define BUMP_ALLOCATOR_H_

#include "core_export.h"
#include <cstddef>
#include <cstdint>
#include <mutex>

class CORE_API MemoryAllocator {
public:
  explicit MemoryAllocator(size_t size);
  virtual ~MemoryAllocator();

  // Disable copy and move
  MemoryAllocator(const MemoryAllocator &) = delete;
  MemoryAllocator &operator=(const MemoryAllocator &) = delete;
  MemoryAllocator(MemoryAllocator &&) = delete;
  MemoryAllocator &operator=(MemoryAllocator &&) = delete;

  virtual void *allocate(size_t size,
                         size_t alignment = alignof(std::max_align_t));

  virtual void *push();
  virtual void *pop();

  virtual void reset();

  [[nodiscard]] const size_t &capacity() const { return total_size_; }

protected:
  mutable std::mutex memoryMutex_;

  [[nodiscard]] const uint8_t *memory() const { return memory_; }

private:
  uint8_t *memory_ = nullptr;
  size_t total_size_;
};

// Bump Allocator for fast allocations
class CORE_API BumpAllocator : public MemoryAllocator {
public:
  explicit BumpAllocator(size_t size);
  ~BumpAllocator();

  void *allocate(size_t size,
                 size_t alignment = alignof(std::max_align_t)) override;

  void reset() override;

  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

// Stock Allocator for variable allocations
class CORE_API StockAllocator : public MemoryAllocator {
public:
  explicit StockAllocator(size_t size);
  ~StockAllocator();

  void *allocate(size_t size,
                 size_t alignment = alignof(std::max_align_t)) override;

  [[nodiscard]] const size_t &bytes_allocated() const { return offset_; }

private:
  size_t offset_;
};

// Pool Allocator for multiple identical allocations
class CORE_API PoolAllocator : public MemoryAllocator {
public:
  explicit PoolAllocator(size_t size);
  ~PoolAllocator();

  void *allocate(size_t size,
                 size_t alignment = alignof(std::max_align_t)) override;
};

#endif // BUMP_ALLOCATOR_H_
