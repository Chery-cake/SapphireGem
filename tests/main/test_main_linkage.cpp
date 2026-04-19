/**
 * @brief Main linkage test
 *
 * Verifies that all three core modules (core, device, window) can be linked
 * together and that their basic APIs are accessible from a single executable.
 */

#include "config.h"
#include "memory_allocator.h"
#include "resource_registry.h"
#include "thread_manager.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"
#include "window.h"
#include <cassert>
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
// Test: Core module APIs are accessible
// ---------------------------------------------------------------------------
void test_core_linkage() {
  TEST(core_linkage);

  // Config singleton
  auto &config = core::Config::instance();
  (void)config.getApplicationConfig();

  // ThreadManager singleton
  auto &threadMgr = core::ThreadManager::instance();
  (void)threadMgr.getPoolNames();

  // BumpAllocator
  core::BumpAllocator<256> alloc;
  assert(alloc.capacity() == 256);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Device module APIs are accessible
// ---------------------------------------------------------------------------
void test_device_linkage() {
  TEST(device_linkage);

  // VulkanInstance construction
  device::VulkanInstance instance;
  assert(!instance.isInitialized());

  // GPUDevice construction
  device::GPUDevice gpuDev;
  assert(!gpuDev.isInitialized());

  // DeviceManager construction
  device::DeviceManager devMgr;
  assert(!devMgr.isInitialized());

  // QueueFamilyIndices
  device::QueueFamilyIndices indices;
  assert(!indices.isComplete());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Window module APIs are accessible
// ---------------------------------------------------------------------------
void test_window_linkage() {
  TEST(window_linkage);

  // WindowConfig
  window::WindowConfig cfg;
  assert(cfg.width == 1280);

  // Window construction
  window::Window win;
  assert(!win.isOpen());

  // WindowManager construction
  window::WindowManager mgr;
  assert(!mgr.isInitialized());

  // WindowEvent
  window::WindowEvent evt;
  assert(evt.type == window::WindowEventType::None);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Cross-module integration (core + device types together)
// ---------------------------------------------------------------------------
void test_cross_module_integration() {
  TEST(cross_module_integration);

  // Use core Config to set application config, then check device types
  auto &config = core::Config::instance();
  core::ApplicationConfig appCfg;
  appCfg.applicationName = "LinkageTest";
  config.setApplicationConfig(appCfg);

  assert(config.getApplicationConfig().applicationName == "LinkageTest");

  // Use core ResourceRegistry with a custom tag
  struct SimpleTag {
    const char *name;
    constexpr SimpleTag(const char *n) : name(n) {}
  };

  class SimpleAsset {
  public:
    explicit SimpleAsset(const SimpleTag &t) : name_(t.name) {}
    const std::string &name() const { return name_; }

  private:
    std::string name_;
  };

  static constexpr SimpleTag TAG{"test"};
  core::ResourceRegistry<SimpleTag, SimpleAsset> registry;
  registry.emplace(&TAG);
  assert(registry.size() == 1);
  assert(registry.get(&TAG)->name() == "test");

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Main Linkage Tests ===\n");

  test_core_linkage();
  test_device_linkage();
  test_window_linkage();
  test_cross_module_integration();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
