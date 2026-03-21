#include "memory_allocator.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

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

  core::BumpAllocator<1024> alloc;
  assert(alloc.capacity() == 1024);
  assert(alloc.bytes_allocated() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Simple allocation
// ---------------------------------------------------------------------------
void test_simple_allocation() {
  TEST(simple_allocation);

  core::BumpAllocator<1024> alloc;
  void *ptr = alloc.allocate<std::array<uint8_t, 64>>();
  assert(ptr != nullptr);
  assert(alloc.bytes_allocated() >= 64);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple allocations
// ---------------------------------------------------------------------------
void test_multiple_allocations() {
  TEST(multiple_allocations);

  core::BumpAllocator<1024> alloc;
  void *p1 = alloc.allocate<std::array<uint8_t, 128>>();
  void *p2 = alloc.allocate<std::array<uint8_t, 128>>();
  void *p3 = alloc.allocate<std::array<uint8_t, 128>>();

  assert(p1 != nullptr);
  assert(p2 != nullptr);
  assert(p3 != nullptr);
  assert(p1 != p2);
  assert(p2 != p3);
  assert(alloc.bytes_allocated() >= 384);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Allocation returns null when out of memory
// ---------------------------------------------------------------------------
void test_out_of_memory() {
  TEST(out_of_memory);

  core::BumpAllocator<64> alloc;
  void *p1 = alloc.allocate<std::array<uint8_t, 32>>();
  assert(p1 != nullptr);

  // This should fail because 64 - 32 = 32, and requesting 64 more
  bool bad_alloc = false;
  try {
    alloc.allocate<std::array<uint8_t, 64>>();
  } catch (const std::bad_alloc &) {
    bad_alloc = true;
  }
  assert(bad_alloc);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Reset clears all allocations
// ---------------------------------------------------------------------------
void test_reset() {
  TEST(reset);

  core::BumpAllocator<256> alloc;
  alloc.allocate<std::array<uint8_t, 128>>();
  assert(alloc.bytes_allocated() >= 128);

  alloc.reset();
  assert(alloc.bytes_allocated() == 0);

  // Should be able to allocate again after reset
  void *ptr = alloc.allocate<std::array<uint8_t, 128>>();
  assert(ptr != nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Aligned allocation
// ---------------------------------------------------------------------------
void test_alignment() {
  TEST(alignment);

  core::BumpAllocator<4096> alloc;

  void *p1 = alloc.allocate<uint8_t>(1); // Misalign intentionally
  assert(p1 != nullptr);
  void *p2 = alloc.allocate<std::array<uint8_t, 64>>(64);
  assert(p2 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p2) % 64 == 0);

  void *p3 = alloc.allocate<std::array<uint8_t, 128>>(128);
  assert(p3 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p3) % 128 == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Capacity is unchanged after allocations
// ---------------------------------------------------------------------------
void test_capacity_unchanged() {
  TEST(capacity_unchanged);

  core::BumpAllocator<512> alloc;
  assert(alloc.capacity() == 512);

  alloc.allocate<std::array<uint8_t, 256>>();
  assert(alloc.capacity() == 512);

  alloc.reset();
  assert(alloc.capacity() == 512);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== BumpAllocator Tests ===\n");

  test_construction();
  test_simple_allocation();
  test_multiple_allocations();
  test_out_of_memory();
  test_reset();
  test_alignment();
  test_capacity_unchanged();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
