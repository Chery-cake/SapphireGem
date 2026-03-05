#include "window.h"
#include <cassert>
#include <cstdio>
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
// Test: WindowConfig defaults
// ---------------------------------------------------------------------------
void test_window_config_defaults() {
  TEST(window_config_defaults);

  window::WindowConfig cfg;
  assert(cfg.title == "SapphireEngine");
  assert(cfg.width == 1280);
  assert(cfg.height == 720);
  assert(cfg.x == -1);
  assert(cfg.y == -1);
  assert(cfg.fullscreen == false);
  assert(cfg.borderless == false);
  assert(cfg.resizable == true);
  assert(cfg.maximized == false);
  assert(cfg.vsync == true);
  assert(cfg.highDPI == true);
  assert(cfg.mainGPU == nullptr);
  assert(cfg.secondaryGPUs.empty());
  assert(cfg.vulkanInstance == nullptr);
  assert(cfg.allocator == nullptr);
  assert(cfg.shaderManager == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowConfig custom values
// ---------------------------------------------------------------------------
void test_window_config_custom() {
  TEST(window_config_custom);

  window::WindowConfig cfg;
  cfg.title = "MyApp";
  cfg.width = 1920;
  cfg.height = 1080;
  cfg.fullscreen = true;
  cfg.vsync = false;

  assert(cfg.title == "MyApp");
  assert(cfg.width == 1920);
  assert(cfg.height == 1080);
  assert(cfg.fullscreen == true);
  assert(cfg.vsync == false);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowEvent defaults
// ---------------------------------------------------------------------------
void test_window_event_defaults() {
  TEST(window_event_defaults);

  window::WindowEvent evt;
  assert(evt.type == window::WindowEventType::None);
  assert(evt.width == 0);
  assert(evt.height == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowEventType enum values
// ---------------------------------------------------------------------------
void test_window_event_types() {
  TEST(window_event_types);

  assert(static_cast<uint8_t>(window::WindowEventType::None) == 0);
  assert(static_cast<uint8_t>(window::WindowEventType::Close) == 1);
  assert(static_cast<uint8_t>(window::WindowEventType::Resize) == 2);
  assert(static_cast<uint8_t>(window::WindowEventType::Minimize) == 3);
  assert(static_cast<uint8_t>(window::WindowEventType::Maximize) == 4);
  assert(static_cast<uint8_t>(window::WindowEventType::Restore) == 5);
  assert(static_cast<uint8_t>(window::WindowEventType::Focus) == 6);
  assert(static_cast<uint8_t>(window::WindowEventType::Unfocus) == 7);
  assert(static_cast<uint8_t>(window::WindowEventType::MouseEnter) == 8);
  assert(static_cast<uint8_t>(window::WindowEventType::MouseLeave) == 9);
  assert(static_cast<uint8_t>(window::WindowEventType::KeyDown) == 10);
  assert(static_cast<uint8_t>(window::WindowEventType::KeyUp) == 11);
  assert(static_cast<uint8_t>(window::WindowEventType::TextInput) == 12);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Window default state (not opened)
// ---------------------------------------------------------------------------
void test_window_default_state() {
  TEST(window_default_state);

  window::Window win;
  assert(!win.isOpen());
  assert(!win.shouldClose());
  assert(!win.hasRenderer());
  assert(!win.hasSwapchain());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Window request close
// ---------------------------------------------------------------------------
void test_window_request_close() {
  TEST(window_request_close);

  window::Window win;
  assert(!win.shouldClose());
  win.requestClose();
  assert(win.shouldClose());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: WindowManager default state
// ---------------------------------------------------------------------------
void test_window_manager_default() {
  TEST(window_manager_default);

  window::WindowManager mgr;
  assert(!mgr.isInitialized());
  assert(mgr.getWindowCount() == 0);
  assert(mgr.checkWindowsVectorEmpty());

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Window Structures Tests ===\n");

  test_window_config_defaults();
  test_window_config_custom();
  test_window_event_defaults();
  test_window_event_types();
  test_window_default_state();
  test_window_request_close();
  test_window_manager_default();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
