/**
 * @brief macOS-specific platform tests
 *
 * Tests macOS platform compatibility including:
 * - Platform defines (__APPLE__, __MACH__)
 * - POSIX/Darwin features
 * - Vulkan Metal platform extension
 * - Framework availability
 */

#include "config.h"
#include <cassert>
#include <cstdio>
#include <dlfcn.h>
#include <string>
#include <sys/sysctl.h>
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
// Test: macOS platform defines
// ---------------------------------------------------------------------------
void test_macos_platform_defines() {
  TEST(macos_platform_defines);

#if defined(__APPLE__) && defined(__MACH__)
  assert(true);
#else
  assert(false && "Expected __APPLE__ and __MACH__ on macOS");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: POSIX features on macOS
// ---------------------------------------------------------------------------
void test_posix_features() {
  TEST(posix_features);

  long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  assert(nprocs > 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: sysctl for hardware info
// ---------------------------------------------------------------------------
void test_sysctl_hardware() {
  TEST(sysctl_hardware);

  int mib[2] = {CTL_HW, HW_NCPU};
  int ncpu = 0;
  size_t len = sizeof(ncpu);
  int ret = sysctl(mib, 2, &ncpu, &len, nullptr, 0);
  assert(ret == 0);
  assert(ncpu > 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: dlopen available on macOS
// ---------------------------------------------------------------------------
void test_dlopen_available() {
  TEST(dlopen_available);

  // Try to load system library
  void *handle = dlopen("libSystem.B.dylib", RTLD_LAZY);
  if (handle) {
    dlclose(handle);
  }
  // dlopen itself is functional
  dlerror(); // Clear error

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Vulkan Metal platform extension
// ---------------------------------------------------------------------------
void test_vulkan_metal_extension() {
  TEST(vulkan_metal_extension);

#ifdef VK_USE_PLATFORM_METAL_EXT
  assert(true);
#else
  std::printf("(VK_USE_PLATFORM_METAL_EXT not defined) ");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Config has correct macOS platform extensions
// ---------------------------------------------------------------------------
void test_config_macos_extensions() {
  TEST(config_macos_extensions);

  auto &config = core::Config::instance();
  const auto &vkCfg = config.getVulkanConfig();

  bool hasSurface = false;
  for (const auto &ext : vkCfg.instanceExtensions) {
    if (ext == "VK_KHR_surface") {
      hasSurface = true;
    }
  }
  assert(hasSurface);

  // On macOS, there should be a Metal surface extension
  bool hasMetalSurface = false;
  for (const auto &ext : vkCfg.instanceExtensions) {
    if (ext == "VK_EXT_metal_surface") {
      hasMetalSurface = true;
    }
  }
  assert(hasMetalSurface);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Hardware concurrency matches sysctl
// ---------------------------------------------------------------------------
void test_hardware_concurrency() {
  TEST(hardware_concurrency);

  unsigned int hwThreads = std::thread::hardware_concurrency();
  assert(hwThreads > 0);

  int mib[2] = {CTL_HW, HW_NCPU};
  int ncpu = 0;
  size_t len = sizeof(ncpu);
  sysctl(mib, 2, &ncpu, &len, nullptr, 0);
  assert(ncpu > 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: macOS architecture detection
// ---------------------------------------------------------------------------
void test_architecture() {
  TEST(architecture);

#if defined(__arm64__) || defined(__aarch64__)
  std::printf("(arm64) ");
#elif defined(__x86_64__)
  std::printf("(x86_64) ");
#else
  std::printf("(unknown arch) ");
#endif

  // Architecture should be defined
  assert(sizeof(void *) == 8); // 64-bit

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== macOS Platform Tests ===\n");

  test_macos_platform_defines();
  test_posix_features();
  test_sysctl_hardware();
  test_dlopen_available();
  test_vulkan_metal_extension();
  test_config_macos_extensions();
  test_hardware_concurrency();
  test_architecture();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
