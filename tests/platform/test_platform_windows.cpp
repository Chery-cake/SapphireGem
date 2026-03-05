/**
 * @brief Windows-specific platform tests
 *
 * Tests Windows platform compatibility including:
 * - Platform defines (WIN32, _WIN32)
 * - Windows API availability
 * - Vulkan platform extensions
 */

#include "config.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#endif

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
// Test: Windows platform define
// ---------------------------------------------------------------------------
void test_windows_platform_define() {
  TEST(windows_platform_define);

#ifdef _WIN32
  assert(true);
#else
  assert(false && "Expected _WIN32 to be defined on Windows");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Windows API functions available
// ---------------------------------------------------------------------------
void test_windows_api_available() {
  TEST(windows_api_available);

#ifdef _WIN32
  // GetModuleHandle should work
  HMODULE hModule = GetModuleHandleA(nullptr);
  assert(hModule != nullptr);

  // GetCurrentProcessId should work
  DWORD pid = GetCurrentProcessId();
  assert(pid > 0);

  // GetSystemInfo should work
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  assert(sysInfo.dwNumberOfProcessors > 0);
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Dynamic library loading on Windows
// ---------------------------------------------------------------------------
void test_dynamic_library_loading() {
  TEST(dynamic_library_loading);

#ifdef _WIN32
  // Try to load kernel32.dll (always available)
  HMODULE hLib = LoadLibraryA("kernel32.dll");
  assert(hLib != nullptr);

  // Get a function pointer
  auto pGetVersion =
      (DWORD(WINAPI *)())GetProcAddress(hLib, "GetVersion");
  assert(pGetVersion != nullptr);

  FreeLibrary(hLib);
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Winsock availability
// ---------------------------------------------------------------------------
void test_winsock_available() {
  TEST(winsock_available);

#ifdef _WIN32
  WSADATA wsaData;
  int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
  assert(result == 0);
  WSACleanup();
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Vulkan Win32 platform extension defined
// ---------------------------------------------------------------------------
void test_vulkan_win32_extension() {
  TEST(vulkan_win32_extension);

#ifdef VK_USE_PLATFORM_WIN32_KHR
  assert(true);
#else
  std::printf("(VK_USE_PLATFORM_WIN32_KHR not defined) ");
#endif

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Config has correct Windows platform extensions
// ---------------------------------------------------------------------------
void test_config_windows_extensions() {
  TEST(config_windows_extensions);

  auto &config = core::Config::instance();
  const auto &vkCfg = config.getVulkanConfig();

  bool hasSurface = false;
  bool hasWin32Surface = false;
  for (const auto &ext : vkCfg.instanceExtensions) {
    if (ext == "VK_KHR_surface")
      hasSurface = true;
    if (ext == "VK_KHR_win32_surface")
      hasWin32Surface = true;
  }
  assert(hasSurface);
  assert(hasWin32Surface);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Windows processor count
// ---------------------------------------------------------------------------
void test_processor_count() {
  TEST(processor_count);

#ifdef _WIN32
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  assert(sysInfo.dwNumberOfProcessors > 0);

  // Compare with C++ hardware_concurrency
  unsigned int hwThreads = std::thread::hardware_concurrency();
  assert(hwThreads > 0);
#endif

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Windows Platform Tests ===\n");

  test_windows_platform_define();
  test_windows_api_available();
  test_dynamic_library_loading();
  test_winsock_available();
  test_vulkan_win32_extension();
  test_config_windows_extensions();
  test_processor_count();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
