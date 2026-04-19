#include "memory_allocator.h"
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <new>
#include <numeric>
#include <ostream>
#include <random>

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

  core::PoolAllocator<int, 10> alloc;
  assert(alloc.capacity() == (sizeof(int) * 10));
  assert(alloc.available() == 10);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Simple allocate
// ---------------------------------------------------------------------------
void test_simple_allocation() {
  TEST(simple_allocation);

  core::PoolAllocator<int, 10> alloc;
  void *ptr = alloc.allocate();
  assert(ptr != nullptr);
  assert(alloc.available() == 9);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple allocations
// ---------------------------------------------------------------------------
void test_multiple_allocations() {
  TEST(multiple_allocations);

  core::PoolAllocator<int, 10> alloc;
  void *p1 = alloc.allocate();
  void *p2 = alloc.allocate();
  void *p3 = alloc.allocate();

  assert(p1 != nullptr);
  assert(p2 != nullptr);
  assert(p3 != nullptr);
  assert(p1 != p2);
  assert(p2 != p3);
  assert(alloc.available() == 7);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Push returns null when out of memory
// ---------------------------------------------------------------------------
void test_out_of_memory() {
  TEST(out_of_memory);

  core::PoolAllocator<int, 10> alloc;
  std::array<void *, 10> ptrs;
  for (auto &ptr : ptrs) {
    ptr = alloc.allocate();
    assert(ptr != nullptr);
  }
  assert(alloc.available() == 0);

  // This should fail because all 10 slots were allocated
  bool bad_alloc = false;
  try {
    alloc.allocate();
  } catch (const std::bad_alloc &) {
    bad_alloc = true;
  }
  assert(bad_alloc);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Deallocate clears allocation
// ---------------------------------------------------------------------------
void test_deallocate() {
  TEST(deallocate);

  core::PoolAllocator<int, 10> alloc;
  std::array<int *, 10> ptrs;

  for (auto &ptr : ptrs) {
    ptr = alloc.allocate();
  }

  for (int i = 9; i > 4; i--) {
    alloc.deallocate(ptrs[i]);
    assert(ptrs[i] == nullptr);
  }
  assert(alloc.available() == 5);

  // Should be able to allocate again after deallocate
  for (int i = 4; i < 9; i++) {
    ptrs[i] = alloc.allocate();
    assert(ptrs[i] != nullptr);
  }
  assert(alloc.available() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Deallocation in random order
// ---------------------------------------------------------------------------
void test_deallocate_random() {
  TEST(deallocate);

  core::PoolAllocator<int, 10> alloc;
  std::array<int *, 10> ptrs;

  for (auto &ptr : ptrs) {
    ptr = alloc.allocate();
  }

  // Create shuffled indices for random deallocation
  std::array<int, 10> indices;
  std::ranges::iota(indices.begin(), indices.end(), 0);
  // Ensure you seed the RNG
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(indices.begin(), indices.end(), g);

  for (int i = 0; i < 5; i++) {
    alloc.deallocate(ptrs[indices[i]]);
    assert(ptrs[indices[i]] == nullptr);
  }
  assert(alloc.available() == 5);

  // Should be able to allocate again after deallocate
  for (int i = 0; i < 5; i++) {
    ptrs[indices[i]] = alloc.allocate();
    assert(ptrs[indices[i]] != nullptr);
  }
  assert(alloc.available() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Capacity is unchanged after allocations
// ---------------------------------------------------------------------------
void test_capacity_unchanged() {
  TEST(capacity_unchanged);

  core::PoolAllocator<int, 10> alloc;
  assert(alloc.capacity() == (sizeof(int) * 10));

  int *ptr = alloc.allocate();
  assert(alloc.capacity() == (sizeof(int) * 10));

  alloc.deallocate(ptr);
  assert(alloc.capacity() == (sizeof(int) * 10));

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== StockAllocator Tests ===\n");

  test_construction();
  test_simple_allocation();
  test_multiple_allocations();
  test_out_of_memory();
  test_deallocate();
  test_deallocate_random();
  test_capacity_unchanged();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
