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

// Lifecycle callbacks that the hot reload system can call
extern "C" {
void lib_on_load(void *data) {
  std::print("[Core] Library loaded!\n");

  coreState *state = static_cast<coreState *>(data);
  
  // If we have saved singleton pointers, restore them
  if (state && state->thread && state->memory) {
    std::print("[Core] Restoring saved singleton instances...\n");
    core::ThreadManager::setInstance(state->thread);
    core::MemoryManager::setInstance(state->memory);
  } else {
    // First load - create new singleton instances
    std::print("[Core] First load - initializing singletons...\n");
    if (state) {
      state->thread = &core::ThreadManager::instance();
      state->memory = &core::MemoryManager::instance();
      state->memory->initialize();
      state->thread->initialize();
    }
  }
  
  std::print("[Core] Singletons ready\n");
}

void lib_on_unload(void *data) {
  std::print("[Core] Library unloading!\n");
  
  coreState *state = static_cast<coreState *>(data);
  
  if (state && state->memory) {
    std::print("[Core] Persistent memory used: {} bytes\n",
               state->memory->getPersistentBytesAllocated());
  }
  
  // DO NOT shutdown or clear the singletons - we want to preserve them!
  // Just clear the global pointers so the library can be unloaded
  std::print("[Core] Preserving singleton instances for reload...\n");
}

void lib_on_reload(void *data) {
  std::print("[Core] Library preparing for reload...\n");
  
  // This is called BEFORE unload
  // Save the current singleton pointers so they can be restored after reload
  coreState *state = static_cast<coreState *>(data);
  if (state) {
    state->thread = core::ThreadManager::getInstance();
    state->memory = core::MemoryManager::getInstance();
    std::print("[Core] Saved singleton pointers for reload\n");
  }
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
