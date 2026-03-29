#include "config.h"
#include "config_threads.h"
#include "config_vulkan.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <iostream>
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
// Test: Singleton access returns the same instance
// ---------------------------------------------------------------------------
void test_singleton() {
  TEST(singleton);

  core::Config &c1 = core::Config::instance();
  core::Config &c2 = core::Config::instance();
  assert(&c1 == &c2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Application config set/get
// ---------------------------------------------------------------------------
void test_application_config() {
  TEST(application_config);

  auto &config = core::Config::instance();

  core::ApplicationConfig appCfg;
  appCfg.applicationName = "TestApp";
  appCfg.applicationVersion = VK_MAKE_VERSION(1, 2, 3);

  config.setApplicationConfig(appCfg);
  const auto &retrieved = config.getApplicationConfig();
  assert(retrieved.applicationName == "TestApp");
  assert(retrieved.applicationVersion == VK_MAKE_VERSION(1, 2, 3));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Vulkan config set/get
// ---------------------------------------------------------------------------
void test_vulkan_config() {
  TEST(vulkan_config);

  auto &config = core::Config::instance();

  core::VulkanConfig &vkCfg = config.getVulkanConfig();
  vkCfg.setEngineName("TestEngine");
  vkCfg.setEngineVersion(VK_MAKE_VERSION(2, 0, 0));

  config.setVulkanConfig(vkCfg);
  const auto &retrieved = config.getVulkanConfig();
  assert(retrieved.getEngineName() == "TestEngine");
  assert(retrieved.getEngineVersion() == VK_MAKE_VERSION(2, 0, 0));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Add/remove instance extensions
// ---------------------------------------------------------------------------
void test_instance_extensions() {
  TEST(instance_extensions);

  auto &config = core::Config::instance().getVulkanConfig();
  config.resetToDefaults();

  config.addInstanceExtension("VK_TEST_extension");

  auto it =
      std::find(config.getInstanceExtensions().begin(),
                config.getInstanceExtensions().end(), "VK_TEST_extension");
  assert(it != config.getInstanceExtensions().end());

  bool removed = config.removeInstanceExtension("VK_TEST_extension");
  assert(removed);

  const auto &vkCfg = core::Config::instance().getVulkanConfig();
  auto it2 =
      std::find(vkCfg.getInstanceExtensions().begin(),
                vkCfg.getInstanceExtensions().end(), "VK_TEST_extension");
  assert(it2 == vkCfg.getInstanceExtensions().end());

  assert(!config.removeInstanceExtension("inexistent"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Add/remove device extensions
// ---------------------------------------------------------------------------
void test_device_extensions() {
  TEST(device_extensions);

  auto &config = core::Config::instance().getVulkanConfig();
  config.resetToDefaults();

  config.addDeviceExtension("VK_TEST_device_ext");

  auto it = std::find(config.getDeviceExtensions().begin(),
                      config.getDeviceExtensions().end(), "VK_TEST_device_ext");
  assert(it != config.getDeviceExtensions().end());

  assert(config.removeDeviceExtension("VK_TEST_device_ext"));
  assert(!config.removeDeviceExtension("inexistent"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Add/remove instance layers
// ---------------------------------------------------------------------------
void test_instance_layers() {
  TEST(instance_layers);

  auto &config = core::Config::instance().getVulkanConfig();
  config.resetToDefaults();

  config.addInstanceLayer("VK_LAYER_TEST_layer");

  auto it = std::find(config.getInstanceLayers().begin(),
                      config.getInstanceLayers().end(), "VK_LAYER_TEST_layer");
  assert(it != config.getInstanceLayers().end());

  assert(config.removeInstanceLayer("VK_LAYER_TEST_layer"));
  assert(!config.removeInstanceLayer("inexistent"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Optional extensions
// ---------------------------------------------------------------------------
void test_optional_extensions() {
  TEST(optional_extensions);

  auto &config = core::Config::instance().getVulkanConfig();
  config.resetToDefaults();

  config.addOptionalInstanceExtension("VK_OPT_inst_ext");
  config.addOptionalDeviceExtension("VK_OPT_dev_ext");
  config.addOptionalInstanceLayer("VK_LAYER_OPT_layer");

  assert(std::find(config.getOptionalInstanceExtensions().begin(),
                   config.getOptionalInstanceExtensions().end(),
                   "VK_OPT_inst_ext") !=
         config.getOptionalInstanceExtensions().end());
  assert(std::find(config.getOptionalDeviceExtensions().begin(),
                   config.getOptionalDeviceExtensions().end(),
                   "VK_OPT_dev_ext") !=
         config.getOptionalDeviceExtensions().end());
  assert(std::find(config.getOptionalInstanceLayers().begin(),
                   config.getOptionalInstanceLayers().end(),
                   "VK_LAYER_OPT_layer") !=
         config.getOptionalInstanceLayers().end());

  assert(config.removeOptionalInstanceExtension("VK_OPT_inst_ext"));
  assert(config.removeOptionalDeviceExtension("VK_OPT_dev_ext"));
  assert(config.removeOptionalInstanceLayer("VK_LAYER_OPT_layer"));

  assert(!config.removeOptionalInstanceExtension("inexistent"));
  assert(!config.removeOptionalDeviceExtension("inexistent"));
  assert(!config.removeOptionalInstanceLayer("inexistent"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Thread pool allocation
// ---------------------------------------------------------------------------
void test_thread_pool_allocation() {
  TEST(thread_pool_allocation);

  auto &config = core::Config::instance().getThreadsConfig();

  core::ThreadPoolAllocation tpa;
  tpa.workerThreads = 8;
  tpa.loopThreads = 2;
  tpa.gpuThreads = 4;
  config.setThreadPoolAllocation(tpa);

  auto retrieved = config.getThreadPoolAllocation();
  assert(retrieved.workerThreads == 8);
  assert(retrieved.loopThreads == 2);
  assert(retrieved.gpuThreads == 4);

  auto effective = config.getEffectiveThreadAllocation();
  assert(effective.workerThreads == 8);
  assert(effective.loopThreads == 2);
  assert(effective.gpuThreads == 4);

  core::ThreadPoolAllocation tpa2;
  tpa2.workerThreads = 0;
  tpa2.loopThreads = 2;
  tpa2.gpuThreads = 4;
  config.setThreadPoolAllocation(tpa2);

  retrieved = config.getThreadPoolAllocation();
  assert(retrieved.workerThreads == 0);
  assert(retrieved.loopThreads == 2);
  assert(retrieved.gpuThreads == 4);

  effective = config.getEffectiveThreadAllocation();
  assert(effective.workerThreads ==
         (std::thread::hardware_concurrency() - effective.loopThreads -
          effective.gpuThreads));
  assert(effective.loopThreads == 2);
  assert(effective.gpuThreads == 4);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPU configuration
// ---------------------------------------------------------------------------
void test_gpu_config() {
  TEST(gpu_config);

  auto &config = core::Config::instance().getThreadsConfig();

  core::GPUConfig gpuCfg;
  gpuCfg.gpuCount = 2;
  gpuCfg.preferredGPUIndex = 1;
  gpuCfg.enableMultiGPU = true;

  config.setGPUConfig(gpuCfg);
  auto retrieved = config.getGPUConfig();
  assert(retrieved.gpuCount == 2);
  assert(retrieved.preferredGPUIndex == 1);
  assert(retrieved.enableMultiGPU == true);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Loop configuration
// ---------------------------------------------------------------------------
void test_loop_config() {
  TEST(loop_config);

  auto &config = core::Config::instance().getThreadsConfig();

  core::LoopConfig loopCfg;
  loopCfg.mainLoopCount = 3;
  loopCfg.targetFrameRate = 144;
  loopCfg.enableVSync = false;
  loopCfg.maxFramesInFlight = 3;

  config.setLoopConfig(loopCfg);
  auto retrieved = config.getLoopConfig();
  assert(retrieved.mainLoopCount == 3);
  assert(retrieved.targetFrameRate == 144);
  assert(retrieved.enableVSync == false);
  assert(retrieved.maxFramesInFlight == 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Configuration change callbacks
// ---------------------------------------------------------------------------
void test_change_callbacks() {
  TEST(change_callbacks);

  auto &config = core::Config::instance();
  config.resetToDefaults();
  config.setImmediateMode(true);

  uint32_t callbackInvoked = 0;

  config.registerChangeCallback("test_callback_1", core::ConfigSection::Vulkan,
                                [&callbackInvoked]() { callbackInvoked++; });
  config.registerChangeCallback("test_callback_2",
                                core::ConfigSection::ThreadPool,
                                [&callbackInvoked]() { callbackInvoked++; });
  config.registerChangeCallback("test_callback_3", core::ConfigSection::GPU,
                                [&callbackInvoked]() { callbackInvoked++; });
  config.registerChangeCallback("test_callback_4", core::ConfigSection::Loop,
                                [&callbackInvoked]() { callbackInvoked++; });
  config.registerChangeCallback("test_callback_5", core::ConfigSection::All,
                                [&callbackInvoked]() { callbackInvoked++; });

  config.getVulkanConfig().addInstanceExtension("VK_TEST_device_ext");
  assert(callbackInvoked == 2);
  config.getVulkanConfig().addInstanceExtension("VK_TEST_device_ext");
  assert(callbackInvoked == 2);

  core::ThreadPoolAllocation threadPool;
  threadPool.workerThreads = 6;
  config.getThreadsConfig().setThreadPoolAllocation(threadPool);
  assert(callbackInvoked == 4);
  config.getThreadsConfig().setThreadPoolAllocation(threadPool);
  assert(callbackInvoked == 4);

  core::GPUConfig gpuCfg;
  gpuCfg.gpuCount = 4;
  config.getThreadsConfig().setGPUConfig(gpuCfg);
  assert(callbackInvoked == 6);
  config.getThreadsConfig().setGPUConfig(gpuCfg);
  assert(callbackInvoked == 6);

  core::LoopConfig loopCfg;
  loopCfg.targetFrameRate = 30;
  config.getThreadsConfig().setLoopConfig(loopCfg);
  assert(callbackInvoked == 8);
  config.getThreadsConfig().setLoopConfig(loopCfg);
  assert(callbackInvoked == 8);

  config.getVulkanConfig().removeInstanceExtension("VK_TEST_device_ext");
  assert(callbackInvoked == 10);
  config.getVulkanConfig().removeInstanceExtension("VK_TEST_device_ext");
  assert(callbackInvoked == 10);

  config.unregisterChangeCallback("test_callback_1");
  config.unregisterChangeCallback("test_callback_2");
  config.unregisterChangeCallback("test_callback_3");
  config.unregisterChangeCallback("test_callback_4");
  config.unregisterChangeCallback("test_callback_5");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Batched mode (non-immediate)
// ---------------------------------------------------------------------------
void test_batched_mode() {
  TEST(batched_mode);

  auto &config = core::Config::instance();
  config.resetToDefaults();
  config.setImmediateMode(false);
  assert(!config.isImmediateMode());

  bool callbackInvoked = false;
  config.registerChangeCallback(
      "batch_callback", core::ConfigSection::Loop,
      [&callbackInvoked]() { callbackInvoked = true; });

  core::LoopConfig loopCfg;
  loopCfg.targetFrameRate = 30;
  config.getThreadsConfig().setLoopConfig(loopCfg);

  // In non-immediate mode, callback should not fire until applyPendingChanges
  assert(!callbackInvoked);

  config.applyPendingChanges();
  assert(callbackInvoked);

  config.unregisterChangeCallback("batch_callback");
  config.setImmediateMode(true);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Reset to defaults
// ---------------------------------------------------------------------------
void test_reset_to_defaults() {
  TEST(reset_to_defaults);

  auto &config = core::Config::instance();

  core::GPUConfig gpuCfg;
  gpuCfg.gpuCount = 10;
  config.getThreadsConfig().setGPUConfig(gpuCfg);

  config.resetToDefaults();

  auto retrieved = config.getThreadsConfig().getGPUConfig();
  assert(retrieved.gpuCount == 1);
  assert(retrieved.enableMultiGPU == false);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get callback names
// ---------------------------------------------------------------------------
void test_callback_names() {
  TEST(callback_names);

  auto &config = core::Config::instance();
  config.resetToDefaults();

  assert(config.registerChangeCallback("cb_alpha", core::ConfigSection::Vulkan,
                                       []() {}));
  assert(!config.registerChangeCallback("cb_alpha", core::ConfigSection::Vulkan,
                                        []() {}));

  config.registerChangeCallback("cb_beta", core::ConfigSection::GPU, []() {});

  auto names = config.getCallbackNames();
  assert(names.size() >= 2);

  bool foundAlpha = false, foundBeta = false;
  for (const auto &n : names) {
    if (n == "cb_alpha")
      foundAlpha = true;
    if (n == "cb_beta")
      foundBeta = true;
  }
  assert(foundAlpha);
  assert(foundBeta);

  assert(config.unregisterChangeCallback("cb_alpha"));
  assert(config.unregisterChangeCallback("cb_beta"));
  assert(!config.unregisterChangeCallback("cb_gama"));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ConfigSection flags
// ---------------------------------------------------------------------------
void test_config_section_flags() {
  TEST(config_section_flags);

  using core::ConfigSection;
  using core::hasFlag;

  auto combined = ConfigSection::Vulkan | ConfigSection::GPU;
  assert(hasFlag(combined, ConfigSection::Vulkan));
  assert(hasFlag(combined, ConfigSection::GPU));
  assert(!hasFlag(combined, ConfigSection::Loop));
  assert(!hasFlag(combined, ConfigSection::ThreadPool));

  assert(hasFlag(ConfigSection::All, ConfigSection::Vulkan));
  assert(hasFlag(ConfigSection::All, ConfigSection::ThreadPool));
  assert(hasFlag(ConfigSection::All, ConfigSection::GPU));
  assert(hasFlag(ConfigSection::All, ConfigSection::Loop));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Struct equality operators
// ---------------------------------------------------------------------------
void test_struct_equality() {
  TEST(struct_equality);

  core::ApplicationConfig a1, a2;
  assert(a1 == a2);
  a2.applicationName = "Different";
  assert(a1 != a2);

  core::ThreadPoolAllocation t1, t2;
  assert(t1 == t2);
  t2.workerThreads = 99;
  assert(t1 != t2);

  core::GPUConfig g1, g2;
  assert(g1 == g2);
  g2.gpuCount = 5;
  assert(g1 != g2);

  core::LoopConfig l1, l2;
  assert(l1 == l2);
  l2.targetFrameRate = 240;
  assert(l1 != l2);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Config Tests ===\n");

  test_singleton();
  test_application_config();
  test_vulkan_config();
  test_instance_extensions();
  test_device_extensions();
  test_instance_layers();
  test_optional_extensions();
  test_thread_pool_allocation();
  test_gpu_config();
  test_loop_config();
  test_change_callbacks();
  test_batched_mode();
  test_reset_to_defaults();
  test_callback_names();
  test_config_section_flags();
  test_struct_equality();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
