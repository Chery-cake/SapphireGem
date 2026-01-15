#ifndef EXPORT_H_
#define EXPORT_H_

// Core export macros for shared library
#include <print>
#ifdef _WIN32
#ifdef CORE_EXPORTS
#define CORE_API __declspec(dllexport)
#else
#define CORE_API __declspec(dllimport)
#endif
#else
#ifdef CORE_EXPORTS
#define CORE_API __attribute__((visibility("default")))
#else
#define CORE_API
#endif
#endif

// Library state (will be reset on reload)
static int call_count = 0;

// Lifecycle callbacks that the hot reload system can call
extern "C" {
  void lib_on_load(void *userData) {
    std::print("[Plugin] Library loaded!  User data: {}\n",
               static_cast<void *>(userData));
    call_count = 0;
  }

  void lib_on_unload(void *userData) {
    std::print("[Plugin] Library unloading!  Total calls: {}\n", call_count);
  }

  void lib_on_reload(void *userData) {
    std::print("[Plugin] Library preparing for reload...\n");
    // Save state here if needed
  }

  // Your actual plugin functions
  void test_print() {
    call_count++;
    std::print("[Plugin] Hello from hot-reloaded library!  Call #{}\n",
               call_count);
  }

  int change(int x) {
    call_count++;
    return (x + 5) % 2;
  }
}

#endif // EXPORT_H_
