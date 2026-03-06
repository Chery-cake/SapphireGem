#include "bump_allocator.h"
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

  BumpAllocator alloc(1024);
  assert(alloc.capacity() == 1024);
  assert(alloc.bytes_allocated() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Simple allocation
// ---------------------------------------------------------------------------
void test_simple_allocation() {
  TEST(simple_allocation);

  BumpAllocator alloc(1024);
  void *ptr = alloc.allocate(64);
  assert(ptr != nullptr);
  assert(alloc.bytes_allocated() >= 64);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple allocations
// ---------------------------------------------------------------------------
void test_multiple_allocations() {
  TEST(multiple_allocations);

  BumpAllocator alloc(1024);
  void *p1 = alloc.allocate(128);
  void *p2 = alloc.allocate(128);
  void *p3 = alloc.allocate(128);

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

  BumpAllocator alloc(64);
  void *p1 = alloc.allocate(32);
  assert(p1 != nullptr);

  // This should fail because 64 - 32 = 32, and requesting 64 more
  void *p2 = alloc.allocate(64);
  assert(p2 == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Reset clears all allocations
// ---------------------------------------------------------------------------
void test_reset() {
  TEST(reset);

  BumpAllocator alloc(256);
  alloc.allocate(128);
  assert(alloc.bytes_allocated() >= 128);

  alloc.reset();
  assert(alloc.bytes_allocated() == 0);

  // Should be able to allocate again after reset
  void *ptr = alloc.allocate(128);
  assert(ptr != nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Aligned allocation
// ---------------------------------------------------------------------------
void test_alignment() {
  TEST(alignment);

  BumpAllocator alloc(4096);

  void *p1 = alloc.allocate(1, 1); // Misalign intentionally
  (void)p1;
  void *p2 = alloc.allocate(64, 64);
  assert(p2 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p2) % 64 == 0);

  void *p3 = alloc.allocate(128, 128);
  assert(p3 != nullptr);
  assert(reinterpret_cast<uintptr_t>(p3) % 128 == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ScopedAlloc helper
// ---------------------------------------------------------------------------
void test_scoped_alloc() {
  TEST(scoped_alloc);

  BumpAllocator alloc(4096);

  ScopedAlloc<int> scoped(alloc, 10);
  int *arr = scoped.get();
  assert(arr != nullptr);

  // Write to the allocated memory
  for (int i = 0; i < 10; ++i) {
    arr[i] = i * 42;
  }

  // Verify values
  for (int i = 0; i < 10; ++i) {
    assert(arr[i] == i * 42);
  }

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Capacity is unchanged after allocations
// ---------------------------------------------------------------------------
void test_capacity_unchanged() {
  TEST(capacity_unchanged);

  BumpAllocator alloc(512);
  assert(alloc.capacity() == 512);

  alloc.allocate(256);
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
  test_scoped_alloc();
  test_capacity_unchanged();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
