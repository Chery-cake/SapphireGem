#include "memory_allocator.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
  do {                                                                         \
    tests_run++;                                                               \
    std::printf("  [TEST] %s ... ", #name);                                    \
  } while (0)

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
    std::printf("PASSED\n");                                                   \
  } while (0)

// ---------------------------------------------------------------------------
// Test: Basic construction and initial state
// ---------------------------------------------------------------------------
void test_construction() {
  TEST(construction);

  core::StockAllocator<1024> alloc;
  assert(alloc.capacity() == 1024);
  assert(alloc.bytes_allocated() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Simple push
// ---------------------------------------------------------------------------
void test_simple_allocation() {
  TEST(simple_push);

  core::StockAllocator<1024> alloc;
  void *ptr = alloc.push<std::array<uint8_t, 64>>();
  assert(ptr != nullptr);
  assert(alloc.bytes_allocated() >= 64 + sizeof(size_t));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple pushes
// ---------------------------------------------------------------------------
void test_multiple_allocations() {
  TEST(multiple_pushes);

  core::StockAllocator<1024> alloc;
  void *p1 = alloc.push<std::array<uint8_t, 128>>();
  void *p2 = alloc.push<std::array<uint8_t, 128>>();
  void *p3 = alloc.push<std::array<uint8_t, 128>>();

  assert(p1 != nullptr);
  assert(p2 != nullptr);
  assert(p3 != nullptr);
  assert(p1 != p2);
  assert(p2 != p3);
  assert(alloc.bytes_allocated() >= 384 + (3 * sizeof(size_t)));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Push returns null when out of memory
// ---------------------------------------------------------------------------
void test_out_of_memory() {
  TEST(out_of_memory);

  core::StockAllocator<64> alloc;
  void *p1 = alloc.push<std::array<uint8_t, 32>>();
  assert(p1 != nullptr);

  // This should fail because 64 - (32 + sizeof(size_t)) < 32, and requesting
  // 64 more
  void *p2 = alloc.push<std::array<uint8_t, 64>>();
  assert(p2 == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Pop clears last allocation
// ---------------------------------------------------------------------------
void test_pop() {
  TEST(pop);

  core::StockAllocator<256> alloc;

  void *ptr1 = alloc.push<std::array<uint8_t, 8>>();
  assert(alloc.bytes_allocated() >= (8 + sizeof(size_t)));
  size_t alloc1 = alloc.bytes_allocated();

  void *ptr2 = alloc.push<std::array<uint8_t, 128>>();
  assert(alloc.bytes_allocated() >= (128 + sizeof(size_t) + alloc1));

  alloc.pop(ptr2);
  assert(ptr1 != nullptr);
  assert(alloc.bytes_allocated() == alloc1);

  // Should be able to push again after pop
  void *ptr3 = alloc.push<std::array<uint8_t, 128>>();
  assert(ptr3 != nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Aligned allocation
// ---------------------------------------------------------------------------
void test_alignment() {
  TEST(alignment);

  core::StockAllocator<4096> alloc;

  void *p1 = alloc.push<uint8_t>(1); // Misalign intentionally
  assert(p1 != nullptr);
  void *p2 = alloc.push<std::array<uint8_t, 64>>(64);
  assert(p2 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p2) % 64 == 0);

  void *p3 = alloc.push<std::array<uint8_t, 128>>(128);
  assert(p3 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p3) % 128 == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Capacity is unchanged after allocations
// ---------------------------------------------------------------------------
void test_capacity_unchanged() {
  TEST(capacity_unchanged);

  core::StockAllocator<512> alloc;
  assert(alloc.capacity() == 512);

  void *ptr = alloc.push<std::array<uint8_t, 256>>();
  assert(alloc.capacity() == 512);

  alloc.pop(ptr);
  assert(alloc.capacity() == 512);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== StockAllocator Tests ===\n");

  test_construction();
  test_simple_allocation();
  test_multiple_allocations();
  test_out_of_memory();
  test_pop();
  test_alignment();
  test_capacity_unchanged();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
