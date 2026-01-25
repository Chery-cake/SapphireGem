#include <cstdlib>
#ifdef ENGINE_DEBUG
#include "hot_reload.h"
#include "core_export_functions.h"
#endif
#include <print>
#include <thread>

#ifdef ENGINE_DEBUG
// In debug mode with hot reload, we need to allocate singletons in main executable
// memory so they persist across library reloads.
static core::MemoryManager *g_persistentMemoryManager = nullptr;
static core::ThreadManager *g_persistentThreadManager = nullptr;

coreState g_coreState = {nullptr, nullptr};

// Cleanup function to be called on exit
void cleanupPersistentSingletons() {
  if (g_persistentThreadManager) {
    g_persistentThreadManager->shutdown();
    delete g_persistentThreadManager;
    g_persistentThreadManager = nullptr;
  }
  if (g_persistentMemoryManager) {
    g_persistentMemoryManager->shutdown();
    delete g_persistentMemoryManager;
    g_persistentMemoryManager = nullptr;
  }
}

void onLoad(void *userData) {
  std::print("[Main] Core library loaded successfully\n");
  
  // Get the symbol and call it
  HotReload *core = static_cast<HotReload *>(userData);
  auto lib_on_load_func = (void (*)(void *))core->getSymbol("lib_on_load");
  if (lib_on_load_func) {
    // If this is the first load, allocate the singletons in main executable memory
    if (!g_persistentMemoryManager) {
      g_persistentMemoryManager = new core::MemoryManager();
      g_persistentThreadManager = new core::ThreadManager();
      g_coreState.memory = g_persistentMemoryManager;
      g_coreState.thread = g_persistentThreadManager;
      
      // Register cleanup function to be called on exit
      std::atexit(cleanupPersistentSingletons);
    }
    lib_on_load_func(&g_coreState);
  }
}

void onUnload(void *userData) {
  std::print("[Main] Core library unloading\n");
  
  // Get the symbol and call it
  HotReload *core = static_cast<HotReload *>(userData);
  auto lib_on_unload_func = (void (*)(void *))core->getSymbol("lib_on_unload");
  if (lib_on_unload_func) {
    lib_on_unload_func(&g_coreState);
  }
  
  // Note: We don't delete the persistent singletons here because this is called
  // during hot reload. They should only be deleted on final application exit,
  // which is handled by the signal handlers or when the HotReload destructor is called.
}

void onReload(void *userData) {
  std::print("[Main] Core library reloading\n");
  
  // Get the symbol and call it
  HotReload *core = static_cast<HotReload *>(userData);
  auto lib_on_reload_func = (void (*)(void *))core->getSymbol("lib_on_reload");
  if (lib_on_reload_func) {
    lib_on_reload_func(&g_coreState);
  }
}
#endif // ENGINE_DEBUG

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

#ifdef ENGINE_DEBUG
  // Initialize hot reload system for core library
  HotReload core("core", "lib/libcored.so");

  // Set the core as user data for the callbacks
  core.setUserData(&core);

  core.registerLoadCallback("InitializeResources", onLoad);
  core.registerUnloadCallback("CleanupResources", onUnload);
  core.registerReloadCallback("PrepareReload", onReload);

  if (!core.load()) {
    std::print(stderr, "Failed to load core library!\n");
    // Cleanup on failure
    if (g_persistentMemoryManager) {
      delete g_persistentMemoryManager;
      g_persistentMemoryManager = nullptr;
    }
    if (g_persistentThreadManager) {
      delete g_persistentThreadManager;
      g_persistentThreadManager = nullptr;
    }
    return 1;
  }

  std::print("\n=== Starting Hot Reload Loop ===\n\n");
#else
  std::print("\n=== Starting Main Loop (Hot Reload Disabled) ===\n\n");
  // In release mode, you would initialize the core library directly
  // For now, just run without hot reload
#endif // ENGINE_DEBUG

  int frame = 0;
  while (true) {
#ifdef ENGINE_DEBUG
    // Check for library changes and reload if needed
    if (core.checkAndReloadIfNeeded()) {
      std::print(">>> Core library reloaded! <<<\n\n");
    }

    // Call frame begin function to reset frame allocator
    auto begin_frame_func = (void (*)())core.getSymbol("begin_frame");
    if (begin_frame_func) {
      begin_frame_func();
    }

    // Call test functions from core library
    auto print_func = (void (*)())core.getSymbol("test_print");
    auto change = (int (*)(int))core.getSymbol("change");

    if (print_func) {
      print_func();
    }

    if (change) {
      int sum = change(2);
      std::print("(2+5)%2={}\n", sum);
    }
#endif // ENGINE_DEBUG

    std::print("Frame {}\n", frame);
    std::print("\n");
    frame++;
    std::this_thread::sleep_for(std::chrono::seconds(2));
  }

  return EXIT_SUCCESS;
}
