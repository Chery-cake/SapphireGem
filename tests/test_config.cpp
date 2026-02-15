#include "config.h"
#include <cassert>
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

TEST(config_singleton) {
  std::print("[Test] Config singleton...\n");
  auto &config1 = core::Config::instance();
  auto &config2 = core::Config::instance();
  ASSERT_TRUE(&config1 == &config2);
  PASS();
  std::print("[Test] Config singleton: PASSED\n");
}

TEST(config_application_config) {
  std::print("[Test] Config application config...\n");
  auto &config = core::Config::instance();
  core::ApplicationConfig appConfig;
  appConfig.applicationName = "TestApp";
  appConfig.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  config.setApplicationConfig(appConfig);

  auto &retrieved = config.getApplicationConfig();
  ASSERT_EQ(retrieved.applicationName, "TestApp");
  ASSERT_EQ(retrieved.applicationVersion, VK_MAKE_VERSION(1, 0, 0));
  PASS();
  std::print("[Test] Config application config: PASSED\n");
}

TEST(config_vulkan_extensions) {
  std::print("[Test] Config vulkan extensions...\n");
  auto &config = core::Config::instance();

  // Test add/remove instance extension
  config.addInstanceExtension("VK_TEST_extension");
  auto &vulkanConfig = config.getVulkanConfig();
  bool found = false;
  for (const auto &ext : vulkanConfig.instanceExtensions) {
    if (ext == "VK_TEST_extension") {
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);

  bool removed = config.removeInstanceExtension("VK_TEST_extension");
  ASSERT_TRUE(removed);

  // Removing again should return false
  removed = config.removeInstanceExtension("VK_TEST_extension");
  ASSERT_TRUE(!removed);

  PASS();
  std::print("[Test] Config vulkan extensions: PASSED\n");
}

TEST(config_thread_pool_allocation) {
  std::print("[Test] Config thread pool allocation...\n");
  auto &config = core::Config::instance();

  core::ThreadPoolAllocation alloc;
  alloc.workerThreads = 4;
  alloc.loopThreads = 2;
  alloc.gpuThreads = 1;
  config.setThreadPoolAllocation(alloc);

  auto retrieved = config.getThreadPoolAllocation();
  ASSERT_EQ(retrieved.workerThreads, 4u);
  ASSERT_EQ(retrieved.loopThreads, 2u);
  ASSERT_EQ(retrieved.gpuThreads, 1u);

  PASS();
  std::print("[Test] Config thread pool allocation: PASSED\n");
}

TEST(config_gpu_config) {
  std::print("[Test] Config GPU config...\n");
  auto &config = core::Config::instance();

  core::GPUConfig gpuConfig;
  gpuConfig.gpuCount = 2;
  gpuConfig.enableMultiGPU = true;
  gpuConfig.preferredGPUIndex = 1;
  config.setGPUConfig(gpuConfig);

  auto retrieved = config.getGPUConfig();
  ASSERT_EQ(retrieved.gpuCount, 2u);
  ASSERT_TRUE(retrieved.enableMultiGPU);
  ASSERT_EQ(retrieved.preferredGPUIndex, 1u);

  PASS();
  std::print("[Test] Config GPU config: PASSED\n");
}

TEST(config_loop_config) {
  std::print("[Test] Config loop config...\n");
  auto &config = core::Config::instance();

  core::LoopConfig loopConfig;
  loopConfig.mainLoopCount = 2;
  loopConfig.targetFrameRate = 144;
  loopConfig.enableVSync = false;
  loopConfig.maxFramesInFlight = 3;
  config.setLoopConfig(loopConfig);

  auto retrieved = config.getLoopConfig();
  ASSERT_EQ(retrieved.mainLoopCount, 2u);
  ASSERT_EQ(retrieved.targetFrameRate, 144u);
  ASSERT_TRUE(!retrieved.enableVSync);
  ASSERT_EQ(retrieved.maxFramesInFlight, 3u);

  PASS();
  std::print("[Test] Config loop config: PASSED\n");
}

TEST(config_change_callbacks) {
  std::print("[Test] Config change callbacks...\n");
  auto &config = core::Config::instance();

  bool callbackCalled = false;
  bool registered = config.registerChangeCallback(
      "test_cb", core::ConfigSection::GPU,
      [&callbackCalled]() { callbackCalled = true; });
  ASSERT_TRUE(registered);

  // Duplicate name should fail
  bool duplicate = config.registerChangeCallback(
      "test_cb", core::ConfigSection::GPU, []() {});
  ASSERT_TRUE(!duplicate);

  // Changing GPU config should trigger callback
  core::GPUConfig gpuConfig;
  gpuConfig.gpuCount = 4;
  config.setGPUConfig(gpuConfig);
  ASSERT_TRUE(callbackCalled);

  // Unregister
  bool unregistered = config.unregisterChangeCallback("test_cb");
  ASSERT_TRUE(unregistered);

  // Get callback names
  auto names = config.getCallbackNames();
  for (const auto &name : names) {
    ASSERT_TRUE(name != "test_cb");
  }

  PASS();
  std::print("[Test] Config change callbacks: PASSED\n");
}

TEST(config_batch_mode) {
  std::print("[Test] Config batch mode...\n");
  auto &config = core::Config::instance();

  int callCount = 0;
  config.registerChangeCallback(
      "batch_test_cb", core::ConfigSection::Loop,
      [&callCount]() { callCount++; });

  // Switch to batch mode
  config.setImmediateMode(false);
  ASSERT_TRUE(!config.isImmediateMode());

  // Make multiple changes - callback should not fire
  core::LoopConfig loop1;
  loop1.targetFrameRate = 30;
  config.setLoopConfig(loop1);
  ASSERT_EQ(callCount, 0);

  // Apply pending changes
  config.applyPendingChanges();
  ASSERT_EQ(callCount, 1);

  // Restore immediate mode
  config.setImmediateMode(true);
  config.unregisterChangeCallback("batch_test_cb");

  PASS();
  std::print("[Test] Config batch mode: PASSED\n");
}

TEST(config_effective_thread_allocation) {
  std::print("[Test] Config effective thread allocation...\n");
  auto &config = core::Config::instance();

  core::GPUConfig gpuConfig;
  gpuConfig.gpuCount = 2;
  gpuConfig.enableMultiGPU = true;
  config.setGPUConfig(gpuConfig);

  core::LoopConfig loopConfig;
  loopConfig.mainLoopCount = 2;
  config.setLoopConfig(loopConfig);

  core::ThreadPoolAllocation alloc;
  alloc.workerThreads = 0; // auto-detect
  alloc.loopThreads = 1;   // per loop
  alloc.gpuThreads = 1;    // per GPU
  config.setThreadPoolAllocation(alloc);

  auto effective = config.getEffectiveThreadAllocation();
  // loopThreads should be 1 * 2 (mainLoopCount) = 2
  ASSERT_EQ(effective.loopThreads, 2u);
  // gpuThreads should be 1 * 2 (gpuCount) = 2
  ASSERT_EQ(effective.gpuThreads, 2u);
  // workerThreads should be auto-calculated
  ASSERT_TRUE(effective.workerThreads >= 1);

  PASS();
  std::print("[Test] Config effective thread allocation: PASSED\n");
}

TEST(config_reset_to_defaults) {
  std::print("[Test] Config reset to defaults...\n");
  auto &config = core::Config::instance();

  // Change something
  core::GPUConfig gpuConfig;
  gpuConfig.gpuCount = 8;
  config.setGPUConfig(gpuConfig);

  // Reset
  config.resetToDefaults();

  auto retrieved = config.getGPUConfig();
  ASSERT_EQ(retrieved.gpuCount, 1u);
  ASSERT_TRUE(!retrieved.enableMultiGPU);

  PASS();
  std::print("[Test] Config reset to defaults: PASSED\n");
}

TEST(config_thread_safety) {
  std::print("[Test] Config thread safety...\n");
  auto &config = core::Config::instance();

  // Access config from multiple threads concurrently
  std::vector<std::thread> threads;
  for (int i = 0; i < 10; ++i) {
    threads.emplace_back([&config, i]() {
      for (int j = 0; j < 100; ++j) {
        core::GPUConfig gpuConfig;
        gpuConfig.gpuCount = static_cast<uint32_t>(i + 1);
        config.setGPUConfig(gpuConfig);
        auto retrieved = config.getGPUConfig();
        (void)retrieved;
      }
    });
  }

  for (auto &t : threads) {
    t.join();
  }

  PASS();
  std::print("[Test] Config thread safety: PASSED\n");
}

int main() {
  std::print("=== Config Tests ===\n\n");
  // Tests are auto-registered via static initialization

  std::print("\n=== Results: {} passed, {} failed ===\n", tests_passed,
             tests_failed);

  // Cleanup
  core::Config::instance().shutdown();

  return tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
