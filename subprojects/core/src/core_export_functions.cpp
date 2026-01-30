#include "config.h"
#ifdef ENGINE_DEBUG

#include "core_export.h"
#include "core_export_struct.h"
#include "memory_manager.h"
#include "thread_manager.h"
#include <print>

// Lifecycle callbacks that the hot reload system can call
extern "C" {
CORE_API void lib_on_load(void *data) {
  std::print("[Core] Library loaded!\n");

  // Initialize memory
  coreState *state = static_cast<coreState *>(data);

  // If we have saved singleton pointers, restore them
  if (state && state->thread && state->memory && state->config) {
    std::print("[Core] Restoring saved singleton instances...\n");
    core::ThreadManager::setInstance(state->thread);
    core::MemoryManager::setInstance(state->memory);
    core::Config::setInstance(state->config);
  } else {
    // First load - create new singleton instances
    std::print("[Core] First load - initializing singletons...\n");
    if (state) {
      state->memory = &core::MemoryManager::instance();
      state->memory->createPersistentAllocator("core",
                                               10 * 1024 * 1024); // 10 MB
      state->memory->createFrameAllocator("core",
                                          5 * 1024 * 1024); // 5 MB
      state->thread = &core::ThreadManager::instance();
      state->thread->applyConfig(core::ThreadManagerConfig());
      state->thread->createPool(
          core::ThreadPoolConfig("main", core::PoolType::Worker, 0));
      state->config = &core::Config::instance();
    }
  }

  std::print("[Core] Singletons ready\n");
}

CORE_API void lib_on_unload(void *data) {
  std::print("[Core] Library unloading!\n");

  coreState *state = static_cast<coreState *>(data);

  if (state && state->memory) {
    std::print("[Core] Persistent memory used: {} bytes\n",
               state->memory->getPersistentBytesAllocated("core"));
  }

  // DO NOT shutdown or clear the singletons - we want to preserve them!
  // Just clear the global pointers so the library can be unloaded
  std::print("[Core] Preserving singleton instances for reload...\n");
}

CORE_API void lib_on_reload(void *data) {
  std::print("[Core] Library preparing for reload...\n");

  // This is called BEFORE unload
  // Save the current singleton pointers so they can be restored after reload
  coreState *state = static_cast<coreState *>(data);
  if (state) {
    state->thread = core::ThreadManager::getInstance();
    state->memory = core::MemoryManager::getInstance();
    state->config = core::Config::getInstance();
    std::print("[Core] Saved singleton pointers for reload\n");
  }
}

// Your actual plugin functions
CORE_API void test_print() {
  // Demonstrate using the memory manager
  auto &memMgr = core::MemoryManager::instance();
  std::print("[Core] Hello from hot-reloaded library!\n");
  std::print("[Core] Persistent memory: {}/{} bytes\n",
             memMgr.getPersistentBytesAllocated("core"),
             memMgr.getPersistentAllocator("core").capacity());
  std::print("[Core] Frame memory: {}/{} bytes\n",
             memMgr.getFrameBytesAllocated("core"),
             memMgr.getFrameAllocator("core").capacity());
}

CORE_API int change(int x) { return (x + 5) % 2; }

// Frame management function
CORE_API void begin_frame() {
  core::MemoryManager::instance().resetAllFrameAllocators();
}
}
#endif
