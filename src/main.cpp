#include "config.h"
#include "renderer.h"
#include "shader_manager.h"
#include "swapchain.h"
#include "thread_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"
#include "window.h"
#include <chrono>
#include <cstdlib>
#include <stop_token>
#ifdef ENGINE_DEBUG
#include "core_export_struct.h"
#include "device_export_struct.h"
#include "hot_reload.h"
#include "window_export_struct.h"
#endif
#include <print>
#include <thread>

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

#ifdef ENGINE_DEBUG

  // Get the directory containing the executable
  std::string exe_path = argv[0];
  std::string exe_dir = exe_path.substr(0, exe_path.find_last_of("/\\"));
  std::string core_path = exe_dir + "/lib/libcored.so";
  std::string device_path = exe_dir + "/lib/libdeviced.so";
  std::string window_path = exe_dir + "/lib/libwindowd.so";

  // Initialize hot reload system for core library
  HotReload core("core", core_path);

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
        core.setData(nullptr);
      }
    }
    // Clear the HotReload data pointer
    core.setData(nullptr);
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
    coreState *state = static_cast<coreState *>(core.getData());
    if (state) {
      if (state->thread) {
        delete state->thread;
      }
      if (state->memory) {
        delete state->memory;
      }
      if (state->config) {
        delete state->config;
      }
      delete state;
      core.setData(nullptr);
    }
    return EXIT_FAILURE;
  }

  // Initialize hot reload for device library
  HotReload deviceHR("device", device_path);
  deviceHR.setData(new deviceState{nullptr, nullptr, nullptr, nullptr});

  deviceHR.registerLoadCallback("device_load", [&deviceHR](void *data) {
    std::print("[Main] Device library loaded successfully\n");
    auto func =
        (void (*)(void *))deviceHR.getSymbol("device_lib_on_load");
    if (func) {
      func(data);
    }
  });

  deviceHR.registerUnloadCallback("device_unload", [&deviceHR](void *data) {
    std::print("[Main] Device library unloading\n");
    auto func =
        (void (*)(void *))deviceHR.getSymbol("device_lib_on_unload");
    if (func) {
      func(data);
    }
  });

  deviceHR.registerReloadCallback("device_reload", [&deviceHR](void *data) {
    std::print("[Main] Device library reloading\n");
    auto func =
        (void (*)(void *))deviceHR.getSymbol("device_lib_on_reload");
    if (func) {
      func(data);
    }
  });

  deviceHR.registerDestroyCallback("device_cleanup", [&deviceHR](void *data) {
    std::print("[Main] Device library cleanup\n");
    void *symbol = deviceHR.getSymbol("device_lib_on_destroy");
    if (symbol) {
      auto func = reinterpret_cast<void (*)(void *)>(symbol);
      func(data);
    } else {
      deviceState *state = static_cast<deviceState *>(data);
      if (state) {
        delete state;
      }
    }
    deviceHR.setData(nullptr);
  });

  if (!deviceHR.load()) {
    std::print(stderr, "Failed to load device library!\n");
    deviceState *state = static_cast<deviceState *>(deviceHR.getData());
    delete state;
    deviceHR.setData(nullptr);
    return EXIT_FAILURE;
  }

  // Initialize hot reload for window library
  HotReload windowHR("window", window_path);
  windowHR.setData(new windowState{nullptr});

  windowHR.registerLoadCallback("window_load", [&windowHR](void *data) {
    std::print("[Main] Window library loaded successfully\n");
    auto func =
        (void (*)(void *))windowHR.getSymbol("window_lib_on_load");
    if (func) {
      func(data);
    }
  });

  windowHR.registerUnloadCallback("window_unload", [&windowHR](void *data) {
    std::print("[Main] Window library unloading\n");
    auto func =
        (void (*)(void *))windowHR.getSymbol("window_lib_on_unload");
    if (func) {
      func(data);
    }
  });

  windowHR.registerReloadCallback("window_reload", [&windowHR](void *data) {
    std::print("[Main] Window library reloading\n");
    auto func =
        (void (*)(void *))windowHR.getSymbol("window_lib_on_reload");
    if (func) {
      func(data);
    }
  });

  windowHR.registerDestroyCallback("window_cleanup", [&windowHR](void *data) {
    std::print("[Main] Window library cleanup\n");
    void *symbol = windowHR.getSymbol("window_lib_on_destroy");
    if (symbol) {
      auto func = reinterpret_cast<void (*)(void *)>(symbol);
      func(data);
    } else {
      windowState *state = static_cast<windowState *>(data);
      if (state) {
        delete state;
      }
    }
    windowHR.setData(nullptr);
  });

  if (!windowHR.load()) {
    std::print(stderr, "Failed to load window library!\n");
    windowState *state = static_cast<windowState *>(windowHR.getData());
    delete state;
    windowHR.setData(nullptr);
    return EXIT_FAILURE;
  }

  std::print("\n=== Starting Hot Reload Loop ===\n");

  auto funcReloadCheck = [&core, &deviceHR, &windowHR]() {
    // Check for library changes and reload if needed
    if (core.checkAndReloadIfNeeded()) {
      std::print(">>> Core library reloaded! <<<\n\n");
    }
    if (deviceHR.checkAndReloadIfNeeded()) {
      std::print(">>> Device library reloaded! <<<\n\n");
    }
    if (windowHR.checkAndReloadIfNeeded()) {
      std::print(">>> Window library reloaded! <<<\n\n");
    }
  };

  std::jthread hotReload;
  hotReload = std::jthread([&funcReloadCheck](std::stop_token stoken) {
    while (!stoken.stop_requested()) {
      funcReloadCheck();
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  });
#endif // ENGINE_DEBUG

  // Configure multi-GPU if available
  core::GPUConfig gpuConfig;
  gpuConfig.enableMultiGPU = true;
  gpuConfig.gpuCount = 0; // Auto-detect
  core::Config::instance().setGPUConfig(gpuConfig);

  device::VulkanInstance inst;
  inst.initialize();

  device::DeviceManager dMan;
  device::VulkanDeviceConfig devConfig;
  devConfig.surface = nullptr;
  devConfig.enableMultiGPU =
      core::Config::instance().getGPUConfig().enableMultiGPU;
  devConfig.preferredGPUIndex =
      core::Config::instance().getGPUConfig().preferredGPUIndex;
  dMan.initialize(inst, devConfig);

  // Update GPU count in config based on actual device count
  gpuConfig.gpuCount = static_cast<uint32_t>(dMan.getDeviceCount());
  core::Config::instance().setGPUConfig(gpuConfig);

  device::VMAManager vMan;
  vMan.initialize(inst.getRaiiInstance(), dMan);

  window::WindowManager wMan;
  wMan.initialize();

  // Build secondary GPU list for windows
  std::vector<device::GPUDevice *> secondaryGPUs;
  for (size_t i = 1; i < dMan.getDeviceCount(); ++i) {
    secondaryGPUs.push_back(dMan.getDevice(static_cast<uint32_t>(i)));
  }

  // Create Window 1
  window::WindowConfig wConf;
  wConf.mainGPU = &dMan.getPrimaryDevice();
  wConf.secondaryGPUs = secondaryGPUs;
  wConf.title = "SapphireEngine - Window 1";
  window::Window *win1 = wMan.createWindow(wConf);

  // Create Window 2
  wConf.title = "SapphireEngine - Window 2";
  window::Window *win2 = wMan.createWindow(wConf);

  // Update loop config to match window count
  {
    core::LoopConfig loopCfg = core::Config::instance().getLoopConfig();
    loopCfg.mainLoopCount = static_cast<uint32_t>(wMan.getWindowCount());
    core::Config::instance().setLoopConfig(loopCfg);
  }

  // Initialize thread pools based on effective thread allocation
  {
    auto &tm = core::ThreadManager::instance();
    auto effectiveAlloc =
        core::Config::instance().getEffectiveThreadAllocation();

    // Create worker pool if not already created
    if (!tm.hasPool("worker")) {
      core::ThreadPoolConfig workerCfg;
      workerCfg.name = "worker";
      workerCfg.type = core::PoolType::Worker;
      workerCfg.threadCount = effectiveAlloc.workerThreads;
      tm.createPool(workerCfg);
      std::print("[Main] Created worker pool with {} threads\n",
                 effectiveAlloc.workerThreads);
    }

    // Create loop pool for window event polling / rendering loops
    if (!tm.hasPool("loop")) {
      core::ThreadPoolConfig loopCfg;
      loopCfg.name = "loop";
      loopCfg.type = core::PoolType::Loop;
      loopCfg.threadCount = effectiveAlloc.loopThreads;
      tm.createPool(loopCfg);
      std::print("[Main] Created loop pool with {} threads\n",
                 effectiveAlloc.loopThreads);
    }

    // Create GPU pool(s) for GPU operations
    if (!tm.hasPool("gpu")) {
      core::ThreadPoolConfig gpuCfg;
      gpuCfg.name = "gpu";
      gpuCfg.type = core::PoolType::GPU;
      gpuCfg.threadCount = effectiveAlloc.gpuThreads;
      tm.createPool(gpuCfg);
      std::print("[Main] Created GPU pool with {} threads\n",
                 effectiveAlloc.gpuThreads);
    }
  }

  // Register close callbacks on both windows
  if (win1) {
    win1->setEventCallback(
        [](const window::WindowEvent &) {
          std::print("[Main] Window 1 close requested\n");
        },
        window::WindowEventType::Close);

    win1->setEventCallback(
        [](const window::WindowEvent &event) {
          std::print("[Main] Window 1 resized to {}x{}\n", event.width,
                     event.height);
        },
        window::WindowEventType::Resize);
  }

  if (win2) {
    win2->setEventCallback(
        [](const window::WindowEvent &) {
          std::print("[Main] Window 2 close requested\n");
        },
        window::WindowEventType::Close);

    win2->setEventCallback(
        [](const window::WindowEvent &event) {
          std::print("[Main] Window 2 resized to {}x{}\n", event.width,
                     event.height);
        },
        window::WindowEventType::Resize);
  }

  // Create swapchains for all windows
  for (const auto &win : wMan.getWindows()) {
    window::SwapchainConfig sConf;
    sConf.vsync = core::Config::instance().getLoopConfig().enableVSync;
    win->createSwap(inst.getRaiiInstance(), sConf);
  }

  device::ShaderManager sMan;
  sMan.initialize(dMan.getPrimaryDevice());

  // Create a renderer for each window
  std::unordered_map<uint32_t, std::unique_ptr<window::Renderer>> renderers;
  for (const auto &win : wMan.getWindows()) {
    if (win->hasSwapchain()) {
      auto renderer = std::make_unique<window::Renderer>();
      if (renderer->initialize(dMan.getPrimaryDevice(), *win->getSwapchain(),
                               vMan.getPrimaryAllocator())) {
        std::print("[Main] Renderer initialized for window: {}\n",
                   win->getTitle());
        renderers[win->getWindowId()] = std::move(renderer);
      } else {
        std::print(stderr,
                   "[Main] Failed to initialize renderer for window: {}\n",
                   win->getTitle());
      }
    }
  }

  // Main loop - runs until all windows are closed
  while (!wMan.checkWindowsVectorEmpty()) {
    wMan.pollAllEvents();

    // Remove windows that should close
    std::vector<window::Window *> toRemove;
    for (const auto &win : wMan.getWindows()) {
      if (win->shouldClose()) {
        toRemove.push_back(win.get());
      }
    }
    for (auto *win : toRemove) {
      std::print("[Main] Destroying closed window: {}\n", win->getTitle());
      // Shutdown renderer for this window before destroying it
      auto it = renderers.find(win->getWindowId());
      if (it != renderers.end()) {
        it->second->shutdown();
        renderers.erase(it);
      }
      wMan.destroyWindow(win);
    }

    if (wMan.checkWindowsVectorEmpty()) {
      break;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  std::print("[Main] All windows closed, shutting down\n");

  // Shutdown all remaining renderers
  for (auto &[id, renderer] : renderers) {
    renderer->shutdown();
  }
  renderers.clear();

  sMan.shutdown();
  wMan.shutdown();
  vMan.shutdown();
  dMan.shutdown();
  inst.shutdown();

#ifdef ENGINE_DEBUG
  hotReload.request_stop();
  hotReload.join();
#endif

  return EXIT_SUCCESS;
}
