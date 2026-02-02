#include "vulkan_instance.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#ifdef ENGINE_DEBUG
#include "core_export_struct.h"
#include "hot_reload.h"
#endif
#include <print>
#include <thread>

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

#ifdef ENGINE_DEBUG
  // Initialize hot reload system for core library
  HotReload core("core", "lib/libcored.so");

  // Create and store the coreState in the HotReload object's data
  core.setData(new coreState{nullptr, nullptr, nullptr});

  // Register load callback
  core.registerLoadCallback("core_load", [&core](void *data) {
    std::print("[Main] Core library loaded successfully\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_load_func = (void (*)(void *))core.getSymbol("lib_on_load");
    if (!lib_on_load_func) {
      std::print(stderr,
                 "[HotReload] Could not find symbol 'lib_on_load': {}\n",
                 dlerror());
    }

    if (lib_on_load_func) {
      lib_on_load_func(state);
    }
  });

  // Register unload
  core.registerUnloadCallback("core_unload", [&core](void *data) {
    std::print("[Main] Core library unloading\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_unload_func = (void (*)(void *))core.getSymbol("lib_on_unload");

    if (lib_on_unload_func) {
      lib_on_unload_func(state);
    }
  });

  // Register a destroy callback to be executed only when core is destroyed
  // This handles final cleanup of singleton resources by calling into the
  // library's cleanup function, which properly clears global singleton pointers.
  // Note: This callback runs while the library is still loaded (before unload()),
  // and the hot reload system ensures single-threaded access during cleanup.
  core.registerDestroyCallback("core_cleanup", [&core](void *data) {
    std::print("[Main] Core library cleanup\n");

    // Call the library's destroy function which handles singleton cleanup
    // This ensures global pointers are cleared after deletion
    void *symbol = core.getSymbol("lib_on_destroy");
    if (symbol) {
      auto lib_on_destroy_func = reinterpret_cast<void (*)(void *)>(symbol);
      lib_on_destroy_func(data);
    } else {
      std::print(stderr,
                 "[Main] Warning: lib_on_destroy not found, manual cleanup\n");
      // Fallback: manual cleanup if symbol not found (shouldn't happen)
      coreState *state = static_cast<coreState *>(data);
      if (state) {
        if (state->thread) {
          state->thread->shutdown();
          delete state->thread;
        }
        if (state->memory) {
          state->memory->shutdown();
          delete state->memory;
        }
        if (state->config) {
          state->config->shutdown();
          delete state->config;
        }
        delete state;
      }
    }
    // Note: The coreState is deleted by lib_on_destroy, so the HotReload data
    // pointer is now invalid. This is the final cleanup before the library unloads.
  });

  // Register reload callback
  core.registerReloadCallback("core_reload", [&core](void *data) {
    std::print("[Main] Core library reloading\n");

    coreState *state = static_cast<coreState *>(data);
    auto lib_on_reload_func = (void (*)(void *))core.getSymbol("lib_on_reload");

    if (lib_on_reload_func) {
      lib_on_reload_func(state);
    }
  });

  if (!core.load()) {
    std::print(stderr, "Failed to load core library!\n");
    // Cleanup on failure
    coreState *s = static_cast<coreState *>(core.getData());
    if (s->memory) {
      delete s->memory;
    }
    if (s->thread) {
      delete s->thread;
    }
    if (s->config) {
      delete s->config;
    }
    delete s;
    return 1;
  }

  std::print("\n=== Starting Hot Reload Loop ===\n\n");
#else
  std::print("\n=== Starting Main Loop (Hot Reload Disabled) ===\n\n");
  // In release mode, you would initialize the core library directly
  // For now, just run without hot reload
#endif // ENGINE_DEBUG

  std::unique_ptr<device::VulkanInstance> inst;
  inst = std::make_unique<device::VulkanInstance>();
  inst->initialize();

  int frame = 0;
  while (frame < 2) {
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
