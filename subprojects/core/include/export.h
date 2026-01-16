#ifndef EXPORT_H_
#define EXPORT_H_

// Core export macros for shared library
#include "memory_manager.h"
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
  std::print("[Core] Library loaded! Initializing memory manager...\n");

  // Initialize memory manager
  core::MemoryManager::instance().initialize();

  std::print("[Core] Memory manager initialized\n");
  call_count = 0;
}

void lib_on_unload(void *userData) {
  std::print("[Core] Library unloading! Total calls: {}\n", call_count);
  std::print("[Core] Persistent memory used: {} bytes\n",
             core::MemoryManager::instance().getPersistentBytesAllocated());

  // Shutdown memory manager
  core::MemoryManager::instance().shutdown();
}

void lib_on_reload(void *userData) {
  std::print("[Core] Library preparing for reload...\n");
  // Save state here if needed
}

// Your actual plugin functions
void test_print() {
  call_count++;

  // Demonstrate using the memory manager
  auto &memMgr = core::MemoryManager::instance();
  std::print("[Core] Hello from hot-reloaded library! Call #{}\n", call_count);
  std::print("[Core] Frame memory: {}/{} bytes\n",
             memMgr.getFrameBytesAllocated(),
             memMgr.getFrameAllocator().capacity());
}

int change(int x) {
  call_count++;
  return (x + 5) % 2;
}

// Frame management function
void begin_frame() { core::MemoryManager::instance().resetFrameAllocator(); }
}

#endif // EXPORT_H_
