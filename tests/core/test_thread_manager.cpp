#include "thread_manager.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <future>
#include <string>
#include <thread>

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

  auto &t1 = core::ThreadManager::instance();
  auto &t2 = core::ThreadManager::instance();
  assert(&t1 == &t2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Create pool
// ---------------------------------------------------------------------------
void test_create_pool() {
  TEST(create_pool);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "test_pool";
  cfg.type = core::PoolType::Worker;
  cfg.threadCount = 2;

  bool created = mgr.createPool(cfg);
  assert(created);
  assert(mgr.hasPool("test_pool"));

  // Duplicate creation should fail
  bool duplicate = mgr.createPool(cfg);
  assert(!duplicate);

  mgr.destroyPool("test_pool");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get pool
// ---------------------------------------------------------------------------
void test_get_pool() {
  TEST(get_pool);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "get_pool_test";
  cfg.threadCount = 1;
  mgr.createPool(cfg);

  auto *pool = mgr.getPool("get_pool_test");
  assert(pool != nullptr);

  auto *nonexistent = mgr.getPool("no_such_pool");
  assert(nonexistent == nullptr);

  mgr.destroyPool("get_pool_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Destroy pool
// ---------------------------------------------------------------------------
void test_destroy_pool() {
  TEST(destroy_pool);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "destroy_pool_test";
  cfg.threadCount = 1;
  mgr.createPool(cfg);

  bool destroyed = mgr.destroyPool("destroy_pool_test");
  assert(destroyed);
  assert(!mgr.hasPool("destroy_pool_test"));

  // Destroying nonexistent should return false
  bool notFound = mgr.destroyPool("destroy_pool_test");
  assert(!notFound);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Submit task
// ---------------------------------------------------------------------------
void test_submit_task() {
  TEST(submit_task);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "submit_test";
  cfg.threadCount = 2;
  mgr.createPool(cfg);

  auto future = mgr.submitTo("submit_test", []() { return 42; });
  int result = future.get();
  assert(result == 42);

  mgr.destroyPool("submit_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Submit task to nonexistent pool throws
// ---------------------------------------------------------------------------
void test_submit_to_nonexistent() {
  TEST(submit_to_nonexistent);

  auto &mgr = core::ThreadManager::instance();

  bool threw = false;
  try {
    mgr.submitTo("nonexistent_pool", []() { return 0; });
  } catch (const std::runtime_error &) {
    threw = true;
  }
  assert(threw);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple tasks execution
// ---------------------------------------------------------------------------
void test_multiple_tasks() {
  TEST(multiple_tasks);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "multi_test";
  cfg.threadCount = 4;
  mgr.createPool(cfg);

  std::atomic<int> counter{0};
  std::vector<std::future<void>> futures;

  for (int i = 0; i < 100; ++i) {
    futures.push_back(
        mgr.submitTo("multi_test", [&counter]() { counter++; }));
  }

  for (auto &f : futures) {
    f.get();
  }

  assert(counter.load() == 100);

  mgr.destroyPool("multi_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Wait all on specific pool
// ---------------------------------------------------------------------------
void test_wait_all() {
  TEST(wait_all);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "wait_test";
  cfg.threadCount = 2;
  mgr.createPool(cfg);

  std::atomic<int> counter{0};
  for (int i = 0; i < 50; ++i) {
    mgr.submitTo("wait_test", [&counter]() { counter++; });
  }

  mgr.waitAll("wait_test");
  assert(counter.load() == 50);

  mgr.destroyPool("wait_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Pool names
// ---------------------------------------------------------------------------
void test_pool_names() {
  TEST(pool_names);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg1;
  cfg1.name = "pool_alpha";
  cfg1.threadCount = 1;
  mgr.createPool(cfg1);

  core::ThreadPoolConfig cfg2;
  cfg2.name = "pool_beta";
  cfg2.threadCount = 1;
  mgr.createPool(cfg2);

  auto names = mgr.getPoolNames();
  bool foundAlpha = false, foundBeta = false;
  for (const auto &n : names) {
    if (n == "pool_alpha")
      foundAlpha = true;
    if (n == "pool_beta")
      foundBeta = true;
  }
  assert(foundAlpha);
  assert(foundBeta);

  mgr.destroyPool("pool_alpha");
  mgr.destroyPool("pool_beta");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Pool config retrieval
// ---------------------------------------------------------------------------
void test_pool_config() {
  TEST(pool_config);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "config_test";
  cfg.type = core::PoolType::Loop;
  cfg.threadCount = 3;
  mgr.createPool(cfg);

  auto retrieved = mgr.getPoolConfig("config_test");
  assert(retrieved.name == "config_test");
  assert(retrieved.type == core::PoolType::Loop);
  assert(retrieved.threadCount == 3);

  mgr.destroyPool("config_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Pool thread count
// ---------------------------------------------------------------------------
void test_pool_thread_count() {
  TEST(pool_thread_count);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "count_test";
  cfg.threadCount = 4;
  mgr.createPool(cfg);

  assert(mgr.getPoolThreadCount("count_test") == 4);
  assert(mgr.getPoolThreadCount("nonexistent") == 0);

  mgr.destroyPool("count_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Resize pool
// ---------------------------------------------------------------------------
void test_resize_pool() {
  TEST(resize_pool);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadPoolConfig cfg;
  cfg.name = "resize_test";
  cfg.threadCount = 2;
  mgr.createPool(cfg);

  assert(mgr.getPoolThreadCount("resize_test") == 2);

  bool resized = mgr.resizePool("resize_test", 4);
  assert(resized);
  assert(mgr.getPoolThreadCount("resize_test") == 4);

  // Resize nonexistent should fail
  bool notFound = mgr.resizePool("nonexistent", 1);
  assert(!notFound);

  mgr.destroyPool("resize_test");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Apply config
// ---------------------------------------------------------------------------
void test_apply_config() {
  TEST(apply_config);

  auto &mgr = core::ThreadManager::instance();

  core::ThreadManagerConfig cfg;
  cfg.totalThreads = 8;
  cfg.loopThreads = 2;
  cfg.gpuThreads = 1;

  mgr.applyConfig(cfg);
  auto retrieved = mgr.getConfig();
  assert(retrieved.totalThreads == 8);
  assert(retrieved.loopThreads == 2);
  assert(retrieved.gpuThreads == 1);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: PoolType enum values
// ---------------------------------------------------------------------------
void test_pool_types() {
  TEST(pool_types);

  assert(static_cast<uint8_t>(core::PoolType::Worker) == 0);
  assert(static_cast<uint8_t>(core::PoolType::Loop) == 1);
  assert(static_cast<uint8_t>(core::PoolType::GPU) == 2);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== ThreadManager Tests ===\n");

  test_singleton();
  test_create_pool();
  test_get_pool();
  test_destroy_pool();
  test_submit_task();
  test_submit_to_nonexistent();
  test_multiple_tasks();
  test_wait_all();
  test_pool_names();
  test_pool_config();
  test_pool_thread_count();
  test_resize_pool();
  test_apply_config();
  test_pool_types();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
