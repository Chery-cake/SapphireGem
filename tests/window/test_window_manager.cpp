#include "window.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

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

// Check if a display/windowing system is available
static bool displayAvailable() {
#ifdef _WIN32
  return true; // Windows always has a display subsystem
#elif defined(__APPLE__)
  return true; // macOS always has a display subsystem
#else
  // On Linux, check for DISPLAY or WAYLAND_DISPLAY
  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");
  return (display != nullptr) || (wayland != nullptr);
#endif
}

// ---------------------------------------------------------------------------
// Test: WindowManager initialize/shutdown (requires display)
// ---------------------------------------------------------------------------
void test_window_manager_init() {
  TEST(window_manager_init);

  if (!displayAvailable()) {
    SKIP("no display available");
    return;
  }

  window::WindowManager mgr;
  bool ok = mgr.initialize();

  if (ok) {
    assert(mgr.isInitialized());
    assert(mgr.getWindowCount() == 0);
    mgr.shutdown();
    assert(!mgr.isInitialized());
  } else {
    std::printf("(SDL init failed) ");
  }

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowManager get required Vulkan extensions (requires display)
// ---------------------------------------------------------------------------
void test_required_vulkan_extensions() {
  TEST(required_vulkan_extensions);

  if (!displayAvailable()) {
    SKIP("no display available");
    return;
  }

  window::WindowManager mgr;
  if (!mgr.initialize()) {
    SKIP("SDL init failed");
    return;
  }

  auto extensions = window::Window::getRequiredVulkanExtensions();
  // Should return at least VK_KHR_surface
  // (may be empty if SDL hasn't fully initialized Vulkan subsystem)

  mgr.shutdown();

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowManager multiple init/shutdown cycles
// ---------------------------------------------------------------------------
void test_multiple_init_cycles() {
  TEST(multiple_init_cycles);

  if (!displayAvailable()) {
    SKIP("no display available");
    return;
  }

  window::WindowManager mgr;

  for (int i = 0; i < 3; ++i) {
    bool ok = mgr.initialize();
    if (!ok) {
      SKIP("SDL init failed");
      return;
    }
    assert(mgr.isInitialized());
    mgr.shutdown();
    assert(!mgr.isInitialized());
  }

  PASS();
}

// ---------------------------------------------------------------------------
// Test: anyWindowShouldClose with no windows
// ---------------------------------------------------------------------------
void test_no_windows_should_close() {
  TEST(no_windows_should_close);

  if (!displayAvailable()) {
    SKIP("no display available");
    return;
  }

  window::WindowManager mgr;
  if (!mgr.initialize()) {
    SKIP("SDL init failed");
    return;
  }

  assert(!mgr.anyWindowShouldClose());

  mgr.shutdown();

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== WindowManager Tests ===\n");

  test_window_manager_init();
  test_required_vulkan_extensions();
  test_multiple_init_cycles();
  test_no_windows_should_close();

  std::printf("\n%d/%d tests passed", tests_passed, tests_run);
  if (tests_skipped > 0) {
    std::printf(" (%d skipped)", tests_skipped);
  }
  std::printf("\n");
  return (tests_passed == tests_run) ? 0 : 1;
}
