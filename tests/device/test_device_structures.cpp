#include "vulkan_device.h"
#include <cassert>
#include <cstdio>
#include <optional>
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
// Test: QueueFamilyIndices default state
// ---------------------------------------------------------------------------
void test_queue_family_indices_default() {
  TEST(queue_family_indices_default);

  device::QueueFamilyIndices indices;
  assert(!indices.hasGraphics());
  assert(!indices.hasCompute());
  assert(!indices.hasTransfer());
  assert(!indices.canPresent());
  assert(!indices.isComplete());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: QueueFamilyIndices with values set
// ---------------------------------------------------------------------------
void test_queue_family_indices_set() {
  TEST(queue_family_indices_set);

  device::QueueFamilyIndices indices;
  indices.graphicsFamily = 0;
  indices.computeFamily = 1;
  indices.transferFamily = 2;
  indices.presentFamily = 0;

  assert(indices.hasGraphics());
  assert(indices.hasCompute());
  assert(indices.hasTransfer());
  assert(indices.canPresent());
  assert(indices.isComplete());
  assert(indices.graphicsFamily.value() == 0);
  assert(indices.computeFamily.value() == 1);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: QueueFamilyIndices partial completion
// ---------------------------------------------------------------------------
void test_queue_family_indices_partial() {
  TEST(queue_family_indices_partial);

  device::QueueFamilyIndices indices;
  indices.graphicsFamily = 0;
  indices.computeFamily = 1;
  // transferFamily and presentFamily not set

  assert(indices.hasGraphics());
  assert(indices.hasCompute());
  assert(!indices.hasTransfer());
  assert(!indices.canPresent());
  assert(!indices.isComplete());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUInfo default state
// ---------------------------------------------------------------------------
void test_gpu_info_default() {
  TEST(gpu_info_default);

  device::GPUInfo info{};
  info.index = 0;
  info.name = "Test GPU";
  info.vendorId = 0x10DE;
  info.deviceId = 0x1234;
  info.supportsCompute = true;
  info.supportsTransfer = true;
  info.supportsPresent = false;

  assert(info.index == 0);
  assert(info.name == "Test GPU");
  assert(info.vendorId == 0x10DE);
  assert(info.deviceId == 0x1234);
  assert(info.supportsCompute == true);
  assert(info.supportsTransfer == true);
  assert(info.supportsPresent == false);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: VulkanDeviceConfig default values
// ---------------------------------------------------------------------------
void test_vulkan_device_config_defaults() {
  TEST(vulkan_device_config_defaults);

  device::VulkanDeviceConfig cfg{};
  assert(cfg.enableMultiGPU == false);
  assert(cfg.preferredGPUIndex == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUDevice default state (not initialized)
// ---------------------------------------------------------------------------
void test_gpu_device_default() {
  TEST(gpu_device_default);

  device::GPUDevice dev;
  assert(!dev.isInitialized());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: DeviceManager default state (not initialized)
// ---------------------------------------------------------------------------
void test_device_manager_default() {
  TEST(device_manager_default);

  device::DeviceManager mgr;
  assert(!mgr.isInitialized());
  assert(mgr.getDeviceCount() == 0);
  assert(!mgr.isMultiGPUEnabled());

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Device Structures Tests ===\n");

  test_queue_family_indices_default();
  test_queue_family_indices_set();
  test_queue_family_indices_partial();
  test_gpu_info_default();
  test_vulkan_device_config_defaults();
  test_gpu_device_default();
  test_device_manager_default();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
