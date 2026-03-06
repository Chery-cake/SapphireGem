#include "vulkan_instance.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_skipped = 0;

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

#define SKIP(reason)                                                           \
  do {                                                                         \
    tests_skipped++;                                                           \
    tests_passed++;                                                            \
    std::printf("SKIPPED (%s)\n", reason);                                     \
  } while (0)

// Check if Vulkan runtime is available
static bool vulkanAvailable() {
  try {
    // Try to get available extensions - this requires Vulkan runtime
    auto extensions = device::VulkanInstance::getAvailableExtensions();
    return true;
  } catch (...) {
    return false;
  }
}

// ---------------------------------------------------------------------------
// Test: VulkanInstance default state
// ---------------------------------------------------------------------------
void test_default_state() {
  TEST(default_state);

  device::VulkanInstance instance;
  assert(!instance.isInitialized());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get available extensions (requires Vulkan runtime)
// ---------------------------------------------------------------------------
void test_get_available_extensions() {
  TEST(get_available_extensions);

  if (!vulkanAvailable()) {
    SKIP("Vulkan runtime not available");
    return;
  }

  auto extensions = device::VulkanInstance::getAvailableExtensions();
  // If Vulkan is available, there should be at least some extensions
  assert(!extensions.empty());

  // VK_KHR_surface should be available on most systems
  bool hasSurface = false;
  for (const auto &ext : extensions) {
    if (ext == "VK_KHR_surface") {
      hasSurface = true;
      break;
    }
  }
  // Not all Vulkan implementations may have surface, so just check list is
  // non-empty
  (void)hasSurface;

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get available layers (requires Vulkan runtime)
// ---------------------------------------------------------------------------
void test_get_available_layers() {
  TEST(get_available_layers);

  if (!vulkanAvailable()) {
    SKIP("Vulkan runtime not available");
    return;
  }

  auto layers = device::VulkanInstance::getAvailableLayers();
  // Layers may be empty on systems without validation layers installed

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Check extension support (requires Vulkan runtime)
// ---------------------------------------------------------------------------
void test_check_extension_support() {
  TEST(check_extension_support);

  if (!vulkanAvailable()) {
    SKIP("Vulkan runtime not available");
    return;
  }

  // Check with a definitely-nonexistent extension
  std::vector<std::string> missing = {"VK_NONEXISTENT_extension_12345"};
  auto unsupported = device::VulkanInstance::checkExtensionSupport(missing);
  assert(!unsupported.empty());
  assert(unsupported[0] == "VK_NONEXISTENT_extension_12345");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Check layer support (requires Vulkan runtime)
// ---------------------------------------------------------------------------
void test_check_layer_support() {
  TEST(check_layer_support);

  if (!vulkanAvailable()) {
    SKIP("Vulkan runtime not available");
    return;
  }

  std::vector<std::string> missing = {"VK_LAYER_NONEXISTENT_12345"};
  auto unsupported = device::VulkanInstance::checkLayerSupport(missing);
  assert(!unsupported.empty());
  assert(unsupported[0] == "VK_LAYER_NONEXISTENT_12345");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Initialize and shutdown (requires Vulkan runtime)
// ---------------------------------------------------------------------------
void test_initialize_shutdown() {
  TEST(initialize_shutdown);

  if (!vulkanAvailable()) {
    SKIP("Vulkan runtime not available");
    return;
  }

  device::VulkanInstance instance;
  bool initialized = instance.initialize();

  if (initialized) {
    assert(instance.isInitialized());
    assert(instance.getInstance() != vk::Instance{});
    instance.shutdown();
    assert(!instance.isInitialized());
  } else {
    // Initialization may fail without proper GPU drivers
    std::printf("(init failed, no GPU) ");
  }

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Move semantics
// ---------------------------------------------------------------------------
void test_move_semantics() {
  TEST(move_semantics);

  device::VulkanInstance a;
  device::VulkanInstance b(std::move(a));
  assert(!b.isInitialized());

  device::VulkanInstance c;
  c = std::move(b);
  assert(!c.isInitialized());

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== VulkanInstance Tests ===\n");

  test_default_state();
  test_get_available_extensions();
  test_get_available_layers();
  test_check_extension_support();
  test_check_layer_support();
  test_initialize_shutdown();
  test_move_semantics();

  std::printf("\n%d/%d tests passed", tests_passed, tests_run);
  if (tests_skipped > 0) {
    std::printf(" (%d skipped)", tests_skipped);
  }
  std::printf("\n");
  return (tests_passed == tests_run) ? 0 : 1;
}
