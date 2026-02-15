#include "thread_manager.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <print>
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

TEST(thread_manager_singleton) {
  std::print("[Test] ThreadManager singleton...\n");
  auto &tm1 = core::ThreadManager::instance();
  auto &tm2 = core::ThreadManager::instance();
  ASSERT_TRUE(&tm1 == &tm2);
  PASS();
  std::print("[Test] ThreadManager singleton: PASSED\n");
}

TEST(thread_manager_create_pool) {
  std::print("[Test] ThreadManager create pool...\n");
  auto &tm = core::ThreadManager::instance();

  core::ThreadPoolConfig config;
  config.name = "test_worker";
  config.type = core::PoolType::Worker;
  config.threadCount = 2;

  bool created = tm.createPool(config);
  ASSERT_TRUE(created);

  // Duplicate should fail
  bool duplicate = tm.createPool(config);
  ASSERT_TRUE(!duplicate);

  ASSERT_TRUE(tm.hasPool("test_worker"));
  ASSERT_TRUE(!tm.hasPool("nonexistent"));

  PASS();
  std::print("[Test] ThreadManager create pool: PASSED\n");
}

TEST(thread_manager_submit_task) {
  std::print("[Test] ThreadManager submit task...\n");
  auto &tm = core::ThreadManager::instance();

  // Ensure pool exists
  if (!tm.hasPool("test_worker")) {
    core::ThreadPoolConfig config;
    config.name = "test_worker";
    config.type = core::PoolType::Worker;
    config.threadCount = 2;
    tm.createPool(config);
  }

  std::atomic<int> counter{0};
  auto future = tm.submitTo("test_worker", [&counter]() {
    counter.fetch_add(1);
    return 42;
  });

  int result = future.get();
  ASSERT_EQ(result, 42);
  ASSERT_EQ(counter.load(), 1);

  PASS();
  std::print("[Test] ThreadManager submit task: PASSED\n");
}

TEST(thread_manager_multiple_tasks) {
  std::print("[Test] ThreadManager multiple tasks...\n");
  auto &tm = core::ThreadManager::instance();

  if (!tm.hasPool("test_worker")) {
    core::ThreadPoolConfig config;
    config.name = "test_worker";
    config.type = core::PoolType::Worker;
    config.threadCount = 2;
    tm.createPool(config);
  }

  std::atomic<int> counter{0};
  std::vector<std::future<void>> futures;

  for (int i = 0; i < 100; ++i) {
    futures.push_back(
        tm.submitTo("test_worker", [&counter]() { counter.fetch_add(1); }));
  }

  for (auto &f : futures) {
    f.get();
  }

  ASSERT_EQ(counter.load(), 100);

  PASS();
  std::print("[Test] ThreadManager multiple tasks: PASSED\n");
}

TEST(thread_manager_resize_pool) {
  std::print("[Test] ThreadManager resize pool...\n");
  auto &tm = core::ThreadManager::instance();

  if (!tm.hasPool("test_worker")) {
    core::ThreadPoolConfig config;
    config.name = "test_worker";
    config.type = core::PoolType::Worker;
    config.threadCount = 2;
    tm.createPool(config);
  }

  bool resized = tm.resizePool("test_worker", 4);
  ASSERT_TRUE(resized);
  ASSERT_EQ(tm.getPoolThreadCount("test_worker"), 4u);

  // Resize nonexistent should fail
  resized = tm.resizePool("nonexistent", 4);
  ASSERT_TRUE(!resized);

  PASS();
  std::print("[Test] ThreadManager resize pool: PASSED\n");
}

TEST(thread_manager_pool_names) {
  std::print("[Test] ThreadManager pool names...\n");
  auto &tm = core::ThreadManager::instance();

  auto names = tm.getPoolNames();
  bool found = false;
  for (const auto &name : names) {
    if (name == "test_worker") {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  PASS();
  std::print("[Test] ThreadManager pool names: PASSED\n");
}

TEST(thread_manager_pool_config) {
  std::print("[Test] ThreadManager pool config...\n");
  auto &tm = core::ThreadManager::instance();

  auto config = tm.getPoolConfig("test_worker");
  ASSERT_EQ(config.name, "test_worker");
  ASSERT_EQ(config.type, core::PoolType::Worker);

  PASS();
  std::print("[Test] ThreadManager pool config: PASSED\n");
}

TEST(thread_manager_wait_all) {
  std::print("[Test] ThreadManager wait all...\n");
  auto &tm = core::ThreadManager::instance();

  std::atomic<int> counter{0};
  for (int i = 0; i < 10; ++i) {
    tm.submitTo("test_worker", [&counter]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      counter.fetch_add(1);
    });
  }

  tm.waitAll("test_worker");
  ASSERT_EQ(counter.load(), 10);

  PASS();
  std::print("[Test] ThreadManager wait all: PASSED\n");
}

TEST(thread_manager_destroy_pool) {
  std::print("[Test] ThreadManager destroy pool...\n");
  auto &tm = core::ThreadManager::instance();

  bool destroyed = tm.destroyPool("test_worker");
  ASSERT_TRUE(destroyed);
  ASSERT_TRUE(!tm.hasPool("test_worker"));

  // Destroying again should fail
  destroyed = tm.destroyPool("test_worker");
  ASSERT_TRUE(!destroyed);

  PASS();
  std::print("[Test] ThreadManager destroy pool: PASSED\n");
}

TEST(thread_manager_apply_config) {
  std::print("[Test] ThreadManager apply config...\n");
  auto &tm = core::ThreadManager::instance();

  core::ThreadManagerConfig config;
  config.totalThreads = 8;
  config.loopThreads = 1;
  config.gpuThreads = 1;
  tm.applyConfig(config);

  auto retrieved = tm.getConfig();
  ASSERT_EQ(retrieved.totalThreads, 8u);
  ASSERT_EQ(retrieved.loopThreads, 1u);
  ASSERT_EQ(retrieved.gpuThreads, 1u);

  PASS();
  std::print("[Test] ThreadManager apply config: PASSED\n");
}

TEST(thread_manager_gpu_pool) {
  std::print("[Test] ThreadManager GPU pool...\n");
  auto &tm = core::ThreadManager::instance();

  bool reconfigureCalled = false;
  core::ThreadPoolConfig gpuConfig;
  gpuConfig.name = "test_gpu";
  gpuConfig.type = core::PoolType::GPU;
  gpuConfig.threadCount = 1;
  gpuConfig.onReconfigure = [&reconfigureCalled](uint32_t) {
    reconfigureCalled = true;
  };

  bool created = tm.createPool(gpuConfig);
  ASSERT_TRUE(created);

  // Apply config should trigger GPU pool reconfigure
  core::ThreadManagerConfig managerConfig;
  managerConfig.totalThreads = 4;
  tm.applyConfig(managerConfig);
  ASSERT_TRUE(reconfigureCalled);

  tm.destroyPool("test_gpu");

  PASS();
  std::print("[Test] ThreadManager GPU pool: PASSED\n");
}

TEST(thread_manager_thread_safety) {
  std::print("[Test] ThreadManager thread safety...\n");
  auto &tm = core::ThreadManager::instance();

  // Create a pool for this test
  core::ThreadPoolConfig config;
  config.name = "ts_test";
  config.type = core::PoolType::Worker;
  config.threadCount = 2;
  tm.createPool(config);

  // Access from multiple threads
  std::vector<std::thread> threads;
  std::atomic<int> counter{0};

  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([&tm, &counter]() {
      for (int j = 0; j < 20; ++j) {
        auto future = tm.submitTo("ts_test", [&counter]() {
          counter.fetch_add(1);
        });
        future.get();
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  ASSERT_EQ(counter.load(), 100);
  tm.destroyPool("ts_test");

  PASS();
  std::print("[Test] ThreadManager thread safety: PASSED\n");
}

int main() {
  std::print("=== ThreadManager Tests ===\n\n");
  // Tests are auto-registered via static initialization

  std::print("\n=== Results: {} passed, {} failed ===\n", tests_passed,
             tests_failed);

  // Cleanup
  core::ThreadManager::instance().shutdown();

  return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
