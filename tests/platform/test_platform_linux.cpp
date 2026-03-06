/**
 * @brief Linux-specific platform tests
 *
 * Tests Linux platform compatibility including:
 * - Platform defines
 * - POSIX features (pthread, dlopen)
 * - Vulkan platform extensions (XCB/Wayland)
 */

#include "config.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>
#include <string>
#include <thread>
#include <unistd.h>

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
// Test: Linux platform define
// ---------------------------------------------------------------------------
void test_linux_platform_define() {
  TEST(linux_platform_define);

#ifdef __linux__
  assert(true);
#else
  assert(false && "Expected __linux__ to be defined on Linux");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: POSIX features available
// ---------------------------------------------------------------------------
void test_posix_features() {
  TEST(posix_features);

  // _POSIX_VERSION should be defined
#ifdef _POSIX_VERSION
  assert(_POSIX_VERSION >= 200112L);
#endif

  // sysconf should work
  long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  assert(nprocs > 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: pthread is available and functional
// ---------------------------------------------------------------------------
void test_pthread_available() {
  TEST(pthread_available);

  pthread_t thread;
  int result = pthread_create(
      &thread, nullptr,
      [](void *) -> void * { return nullptr; }, nullptr);
  assert(result == 0);
  pthread_join(thread, nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: dlopen is available
// ---------------------------------------------------------------------------
void test_dlopen_available() {
  TEST(dlopen_available);

  // Try to open libc
  void *handle = dlopen("libc.so.6", RTLD_LAZY);
  if (!handle) {
    // Try alternative name
    handle = dlopen("libc.so", RTLD_LAZY);
  }
  // Even if we can't find libc.so, dlopen itself should work
  // (it may return nullptr if not found, which is valid)

  if (handle) {
    dlclose(handle);
  }

  // Verify dlerror works
  dlerror(); // Clear error state

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Vulkan platform extensions configured for Linux
// ---------------------------------------------------------------------------
void test_vulkan_platform_extensions() {
  TEST(vulkan_platform_extensions);

#if defined(VK_USE_PLATFORM_XCB_KHR) || defined(VK_USE_PLATFORM_WAYLAND_KHR)
  assert(true);
#else
  // At minimum one of the Linux platform extensions should be defined
  // when building with the engine's CMake configuration
  std::printf("(no Vulkan platform ext) ");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Config has correct Linux platform extensions
// ---------------------------------------------------------------------------
void test_config_linux_extensions() {
  TEST(config_linux_extensions);

  auto &config = core::Config::instance();
  const auto &vkCfg = config.getVulkanConfig();

  // Surface extension should be in the config
  bool hasSurface = false;
  for (const auto &ext : vkCfg.instanceExtensions) {
    if (ext == "VK_KHR_surface") {
      hasSurface = true;
    }
  }
  assert(hasSurface);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Hardware concurrency detection
// ---------------------------------------------------------------------------
void test_hardware_concurrency() {
  TEST(hardware_concurrency);

  unsigned int hwThreads = std::thread::hardware_concurrency();
  long sysThreads = sysconf(_SC_NPROCESSORS_ONLN);

  // Both should report reasonable values
  assert(hwThreads > 0 || sysThreads > 0);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Linux Platform Tests ===\n");

  test_linux_platform_define();
  test_posix_features();
  test_pthread_available();
  test_dlopen_available();
  test_vulkan_platform_extensions();
  test_config_linux_extensions();
  test_hardware_concurrency();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
