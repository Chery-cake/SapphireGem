#include "memory_manager.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>

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
// Test: Singleton access
// ---------------------------------------------------------------------------
void test_singleton() {
  TEST(singleton);

  auto &m1 = core::MemoryManager::instance();
  auto &m2 = core::MemoryManager::instance();
  assert(&m1 == &m2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Create and get persistent allocator
// ---------------------------------------------------------------------------
void test_persistent_allocator() {
  TEST(persistent_allocator);

  auto &mgr = core::MemoryManager::instance();

  auto &alloc = mgr.createPersistentAllocator("test_persist", 1024);
  assert(alloc.capacity() == 1024);
  assert(alloc.bytes_allocated() == 0);

  assert(mgr.hasPersistentAllocator("test_persist"));
  assert(!mgr.hasPersistentAllocator("nonexistent"));

  auto &retrieved = mgr.getPersistentAllocator("test_persist");
  assert(&alloc == &retrieved);

  mgr.destroyPersistentAllocator("test_persist");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Create and get frame allocator
// ---------------------------------------------------------------------------
void test_frame_allocator() {
  TEST(frame_allocator);

  auto &mgr = core::MemoryManager::instance();

  auto &alloc = mgr.createFrameAllocator("test_frame", 2048);
  assert(alloc.capacity() == 2048);

  assert(mgr.hasFrameAllocator("test_frame"));

  auto &retrieved = mgr.getFrameAllocator("test_frame");
  assert(&alloc == &retrieved);

  mgr.destroyFrameAllocator("test_frame");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Duplicate allocator creation throws
// ---------------------------------------------------------------------------
void test_duplicate_creation() {
  TEST(duplicate_creation);

  auto &mgr = core::MemoryManager::instance();

  mgr.createPersistentAllocator("dup_test", 512);

  bool threw = false;
  try {
    mgr.createPersistentAllocator("dup_test", 512);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  mgr.destroyPersistentAllocator("dup_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Zero-size allocator creation throws
// ---------------------------------------------------------------------------
void test_zero_size_creation() {
  TEST(zero_size_creation);

  auto &mgr = core::MemoryManager::instance();

  bool threw = false;
  try {
    mgr.createPersistentAllocator("zero_test", 0);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Destroy allocator
// ---------------------------------------------------------------------------
void test_destroy_allocator() {
  TEST(destroy_allocator);

  auto &mgr = core::MemoryManager::instance();

  mgr.createPersistentAllocator("destroy_test", 256);
  assert(mgr.hasPersistentAllocator("destroy_test"));

  bool destroyed = mgr.destroyPersistentAllocator("destroy_test");
  assert(destroyed);
  assert(!mgr.hasPersistentAllocator("destroy_test"));

  // Destroying nonexistent should return false
  bool notFound = mgr.destroyPersistentAllocator("destroy_test");
  assert(!notFound);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Reset frame allocators
// ---------------------------------------------------------------------------
void test_reset_frame_allocators() {
  TEST(reset_frame_allocators);

  auto &mgr = core::MemoryManager::instance();

  auto &alloc = mgr.createFrameAllocator("reset_test", 1024);
  alloc.allocate(256);
  assert(alloc.bytes_allocated() >= 256);

  mgr.resetFrameAllocator("reset_test");
  assert(alloc.bytes_allocated() == 0);

  mgr.destroyFrameAllocator("reset_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Reset all frame allocators
// ---------------------------------------------------------------------------
void test_reset_all_frame_allocators() {
  TEST(reset_all_frame_allocators);

  auto &mgr = core::MemoryManager::instance();

  auto &a1 = mgr.createFrameAllocator("frame_a", 512);
  auto &a2 = mgr.createFrameAllocator("frame_b", 512);

  a1.allocate(64);
  a2.allocate(128);

  mgr.resetAllFrameAllocators();
  assert(a1.bytes_allocated() == 0);
  assert(a2.bytes_allocated() == 0);

  mgr.destroyFrameAllocator("frame_a");
  mgr.destroyFrameAllocator("frame_b");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get bytes allocated
// ---------------------------------------------------------------------------
void test_bytes_allocated() {
  TEST(bytes_allocated);

  auto &mgr = core::MemoryManager::instance();

  auto &alloc = mgr.createPersistentAllocator("bytes_test", 1024);
  assert(mgr.getPersistentBytesAllocated("bytes_test") == 0);

  alloc.allocate(100);
  assert(mgr.getPersistentBytesAllocated("bytes_test") >= 100);

  // Nonexistent allocator returns 0
  assert(mgr.getPersistentBytesAllocated("nonexistent") == 0);

  mgr.destroyPersistentAllocator("bytes_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get allocator names
// ---------------------------------------------------------------------------
void test_allocator_names() {
  TEST(allocator_names);

  auto &mgr = core::MemoryManager::instance();

  mgr.createPersistentAllocator("name_a", 256);
  mgr.createPersistentAllocator("name_b", 256);

  auto names = mgr.getPersistentAllocatorNames();
  assert(names.size() >= 2);

  bool foundA = false, foundB = false;
  for (const auto &n : names) {
    if (n == "name_a")
      foundA = true;
    if (n == "name_b")
      foundB = true;
  }
  assert(foundA);
  assert(foundB);

  mgr.destroyPersistentAllocator("name_a");
  mgr.destroyPersistentAllocator("name_b");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Shutdown clears all allocators
// ---------------------------------------------------------------------------
void test_shutdown() {
  TEST(shutdown);

  auto &mgr = core::MemoryManager::instance();

  mgr.createPersistentAllocator("shutdown_p", 128);
  mgr.createFrameAllocator("shutdown_f", 128);

  mgr.shutdown();

  assert(!mgr.hasPersistentAllocator("shutdown_p"));
  assert(!mgr.hasFrameAllocator("shutdown_f"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get nonexistent allocator throws
// ---------------------------------------------------------------------------
void test_get_nonexistent() {
  TEST(get_nonexistent);

  auto &mgr = core::MemoryManager::instance();

  bool threw = false;
  try {
    mgr.getPersistentAllocator("does_not_exist");
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  threw = false;
  try {
    mgr.getFrameAllocator("does_not_exist");
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== MemoryManager Tests ===\n");

  test_singleton();
  test_persistent_allocator();
  test_frame_allocator();
  test_duplicate_creation();
  test_zero_size_creation();
  test_destroy_allocator();
  test_reset_frame_allocators();
  test_reset_all_frame_allocators();
  test_bytes_allocated();
  test_allocator_names();
  test_shutdown();
  test_get_nonexistent();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
