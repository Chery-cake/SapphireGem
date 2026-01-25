#ifndef EXPORT_FUNCTIONS_H_
#define EXPORT_FUNCTIONS_H_

#ifdef ENGINE_DEBUG

#include "memory_manager.h"
#include "thread_manager.h"
#include <print>

struct coreState {
  core::ThreadManager *thread;
  core::MemoryManager *memory;
};

static coreState *state;

// Lifecycle callbacks that the hot reload system can call
extern "C" {
void lib_on_load(void *data) {
  std::print("[Core] Library loaded! Initializing memory manager...\n");

  // Initialize memory
  state = static_cast<coreState *>(data);
  state->thread = &core::ThreadManager::instance();
  state->memory = &core::MemoryManager::instance();

  std::print("[Core] Memory manager initialized\n");
}

void lib_on_unload(void *data) {
  std::print("[Core] Library unloading!\n");
  std::print("[Core] Persistent memory used: {} bytes\n",
             core::MemoryManager::instance().getPersistentBytesAllocated());

  // Clear memory
  state = static_cast<coreState *>(data);
  state->thread->shutdown();
  state->memory->shutdown();
}

void lib_on_reload(void *data) {
  std::print("[Core] Library preparing for reload...\n");
  // Save state here if needed
  state = static_cast<coreState *>(data);
  state->thread = &core::ThreadManager::instance();
  state->memory = &core::MemoryManager::instance();
}

// Your actual plugin functions
void test_print() {
  // Demonstrate using the memory manager
  auto &memMgr = core::MemoryManager::instance();
  std::print("[Core] Hello from hot-reloaded library!\n");
  std::print("[Core] Frame memory: {}/{} bytes\n",
             memMgr.getFrameBytesAllocated(),
             memMgr.getFrameAllocator().capacity());
}

int change(int x) { return (x + 5) % 2; }

// Frame management function
void begin_frame() { core::MemoryManager::instance().resetFrameAllocator(); }
}
#endif

#endif // EXPORT_FUNCTIONS_H_
