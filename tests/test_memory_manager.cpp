#include "memory_manager.h"
#include <cassert>
#include <cstdlib>
#include <print>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
  static void test_##name();                                                   \
  struct Register_##name {                                                     \
    Register_##name() { test_##name(); }                                       \
  } register_##name;                                                           \
  static void test_##name()

#define ASSERT_TRUE(expr)                                                      \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::print(stderr, "  FAIL: {} (line {})\n", #expr, __LINE__);           \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_EQ(a, b)                                                        \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      std::print(stderr, "  FAIL: {} != {} (line {})\n", #a, #b, __LINE__);    \
      tests_failed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
  } while (0)

TEST(memory_manager_singleton) {
  std::print("[Test] MemoryManager singleton...\n");
  auto &mm1 = core::MemoryManager::instance();
  auto &mm2 = core::MemoryManager::instance();
  ASSERT_TRUE(&mm1 == &mm2);
  PASS();
  std::print("[Test] MemoryManager singleton: PASSED\n");
}

TEST(memory_manager_create_persistent) {
  std::print("[Test] MemoryManager create persistent allocator...\n");
  auto &mm = core::MemoryManager::instance();

  auto &alloc = mm.createPersistentAllocator("test_persistent", 1024);
  ASSERT_TRUE(mm.hasPersistentAllocator("test_persistent"));
  ASSERT_TRUE(!mm.hasPersistentAllocator("nonexistent"));

  // Allocate some memory
  void *ptr = alloc.allocate(64, 8);
  ASSERT_TRUE(ptr != nullptr);
  ASSERT_TRUE(alloc.bytes_allocated() >= 64);

  // Duplicate should throw
  try {
    mm.createPersistentAllocator("test_persistent", 1024);
    ASSERT_TRUE(false); // Should not reach here
  } catch (const std::runtime_error &) {
    // Expected
  }

  PASS();
  std::print("[Test] MemoryManager create persistent allocator: PASSED\n");
}

TEST(memory_manager_create_frame) {
  std::print("[Test] MemoryManager create frame allocator...\n");
  auto &mm = core::MemoryManager::instance();

  auto &alloc = mm.createFrameAllocator("test_frame", 2048);
  ASSERT_TRUE(mm.hasFrameAllocator("test_frame"));

  // Allocate and reset
  void *ptr = alloc.allocate(128, 8);
  ASSERT_TRUE(ptr != nullptr);
  ASSERT_TRUE(alloc.bytes_allocated() >= 128);

  mm.resetFrameAllocator("test_frame");
  ASSERT_EQ(alloc.bytes_allocated(), 0u);

  PASS();
  std::print("[Test] MemoryManager create frame allocator: PASSED\n");
}

TEST(memory_manager_get_allocators) {
  std::print("[Test] MemoryManager get allocators...\n");
  auto &mm = core::MemoryManager::instance();

  auto &persistent = mm.getPersistentAllocator("test_persistent");
  ASSERT_TRUE(&persistent != nullptr);

  auto &frame = mm.getFrameAllocator("test_frame");
  ASSERT_TRUE(&frame != nullptr);

  // Getting nonexistent should throw
  try {
    mm.getPersistentAllocator("nonexistent");
    ASSERT_TRUE(false);
  } catch (const std::runtime_error &) {
    // Expected
  }

  PASS();
  std::print("[Test] MemoryManager get allocators: PASSED\n");
}

TEST(memory_manager_bytes_allocated) {
  std::print("[Test] MemoryManager bytes allocated...\n");
  auto &mm = core::MemoryManager::instance();

  auto bytes = mm.getPersistentBytesAllocated("test_persistent");
  ASSERT_TRUE(bytes >= 64); // We allocated 64 earlier

  auto frameBytes = mm.getFrameBytesAllocated("test_frame");
  ASSERT_EQ(frameBytes, 0u); // We reset earlier

  // Nonexistent should return 0
  auto nonexistent = mm.getPersistentBytesAllocated("nonexistent");
  ASSERT_EQ(nonexistent, 0u);

  PASS();
  std::print("[Test] MemoryManager bytes allocated: PASSED\n");
}

TEST(memory_manager_allocator_names) {
  std::print("[Test] MemoryManager allocator names...\n");
  auto &mm = core::MemoryManager::instance();

  auto persistentNames = mm.getPersistentAllocatorNames();
  bool found = false;
  for (const auto &name : persistentNames) {
    if (name == "test_persistent") {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  auto frameNames = mm.getFrameAllocatorNames();
  found = false;
  for (const auto &name : frameNames) {
    if (name == "test_frame") {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  PASS();
  std::print("[Test] MemoryManager allocator names: PASSED\n");
}

TEST(memory_manager_reset_all_frame) {
  std::print("[Test] MemoryManager reset all frame allocators...\n");
  auto &mm = core::MemoryManager::instance();

  // Create another frame allocator
  auto &alloc2 = mm.createFrameAllocator("test_frame2", 1024);
  alloc2.allocate(64, 8);
  auto &alloc1 = mm.getFrameAllocator("test_frame");
  alloc1.allocate(64, 8);

  mm.resetAllFrameAllocators();

  ASSERT_EQ(mm.getFrameBytesAllocated("test_frame"), 0u);
  ASSERT_EQ(mm.getFrameBytesAllocated("test_frame2"), 0u);

  PASS();
  std::print("[Test] MemoryManager reset all frame allocators: PASSED\n");
}

TEST(memory_manager_destroy_allocators) {
  std::print("[Test] MemoryManager destroy allocators...\n");
  auto &mm = core::MemoryManager::instance();

  bool destroyed = mm.destroyPersistentAllocator("test_persistent");
  ASSERT_TRUE(destroyed);
  ASSERT_TRUE(!mm.hasPersistentAllocator("test_persistent"));

  // Destroy again should fail
  destroyed = mm.destroyPersistentAllocator("test_persistent");
  ASSERT_TRUE(!destroyed);

  destroyed = mm.destroyFrameAllocator("test_frame");
  ASSERT_TRUE(destroyed);

  destroyed = mm.destroyFrameAllocator("test_frame2");
  ASSERT_TRUE(destroyed);

  PASS();
  std::print("[Test] MemoryManager destroy allocators: PASSED\n");
}

TEST(memory_manager_zero_size) {
  std::print("[Test] MemoryManager zero size allocator...\n");
  auto &mm = core::MemoryManager::instance();

  // Zero size should throw
  try {
    mm.createPersistentAllocator("zero_size", 0);
    ASSERT_TRUE(false);
  } catch (const std::runtime_error &) {
    // Expected
  }

  PASS();
  std::print("[Test] MemoryManager zero size allocator: PASSED\n");
}

TEST(memory_manager_thread_safety) {
  std::print("[Test] MemoryManager thread safety...\n");
  auto &mm = core::MemoryManager::instance();

  // Create allocators from multiple threads
  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([&mm, i]() {
      std::string name = "ts_alloc_" + std::to_string(i);
      try {
        mm.createPersistentAllocator(name, 1024);
      } catch (...) {
        // May already exist from concurrent creation
      }

      // Read operations
      for (int j = 0; j < 50; ++j) {
        mm.hasPersistentAllocator(name);
        mm.getPersistentBytesAllocated(name);
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  // Cleanup
  for (int i = 0; i < 5; ++i) {
    std::string name = "ts_alloc_" + std::to_string(i);
    mm.destroyPersistentAllocator(name);
  }

  PASS();
  std::print("[Test] MemoryManager thread safety: PASSED\n");
}

int main() {
  std::print("=== MemoryManager Tests ===\n\n");
  // Tests are auto-registered via static initialization

  std::print("\n=== Results: {} passed, {} failed ===\n", tests_passed,
             tests_failed);

  // Cleanup
  core::MemoryManager::instance().shutdown();

  return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
