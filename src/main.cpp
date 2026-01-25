#include <cstdlib>
#ifdef ENGINE_DEBUG
#include "hot_reload.h"
#include "core_export_functions.h"
#endif
#include <print>
#include <thread>

#ifdef ENGINE_DEBUG
// No global state needed - everything is managed by HotReload object
#endif // ENGINE_DEBUG

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

#ifdef ENGINE_DEBUG
  // Initialize hot reload system for core library
  HotReload core("core", "lib/libcored.so");

  // Create and store the coreState in the HotReload object's data
  coreState *state = new coreState{nullptr, nullptr};
  core.setUserData(state);

  // Register load callback using lambda for easier identification
  core.registerLoadCallback("core_load", [&core](void *userData) {
    std::print("[Main] Core library loaded successfully\n");
    
    coreState *state = static_cast<coreState *>(userData);
    auto lib_on_load_func = (void (*)(void *))core.getSymbol("lib_on_load");
    
    if (lib_on_load_func) {
      // If this is the first load, allocate the singletons in main executable memory
      if (!state->memory) {
        state->memory = new core::MemoryManager();
        state->thread = new core::ThreadManager();
      }
      lib_on_load_func(state);
    }
  });

  // Register unload callback using lambda
  core.registerUnloadCallback("core_unload", [&core](void *userData) {
    std::print("[Main] Core library unloading\n");
    
    coreState *state = static_cast<coreState *>(userData);
    auto lib_on_unload_func = (void (*)(void *))core.getSymbol("lib_on_unload");
    
    if (lib_on_unload_func) {
      lib_on_unload_func(state);
    }
    
    // On final unload (when HotReload is being destroyed), cleanup the state
    // This will be called last when the core object goes out of scope
  });
  
  // Register a cleanup callback to be executed when core is destroyed
  // Using a separate lambda to handle final cleanup
  core.registerUnloadCallback("core_cleanup", [](void *userData) {
    coreState *state = static_cast<coreState *>(userData);
    if (state) {
      if (state->thread) {
        state->thread->shutdown();
        delete state->thread;
      }
      if (state->memory) {
        state->memory->shutdown();
        delete state->memory;
      }
      delete state;
    }
  });

  // Register reload callback using lambda
  core.registerReloadCallback("core_reload", [&core](void *userData) {
    std::print("[Main] Core library reloading\n");
    
    coreState *state = static_cast<coreState *>(userData);
    auto lib_on_reload_func = (void (*)(void *))core.getSymbol("lib_on_reload");
    
    if (lib_on_reload_func) {
      lib_on_reload_func(state);
    }
  });

  if (!core.load()) {
    std::print(stderr, "Failed to load core library!\n");
    // Cleanup on failure
    if (state->memory) {
      delete state->memory;
    }
    if (state->thread) {
      delete state->thread;
    }
    delete state;
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
