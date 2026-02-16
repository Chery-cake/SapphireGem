#include "material.h"
#include "object.h"
#include "renderer.h"
#include "resource_registry.h"
#include "shader_manager.h"
#include "swapchain.h"
#include "texture.h"
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
#include "hot_reload.h"
#endif
#include <print>
#include <thread>

// ============================================================================
// Static shader, material, and object tags (must have static storage duration)
// ============================================================================

// Shader tags for the triangle shader program
static constexpr device::ShaderTag TRIANGLE_SHADER_TAG{
    "triangle", "triangle.slang", "vertMain", "fragMain", "geomMain"};

// Material tags (one per window/object)
static constexpr device::MaterialTag MATERIAL_WIN1_TAG{
    "material_win1", &TRIANGLE_SHADER_TAG};
static constexpr device::MaterialTag MATERIAL_WIN2_TAG{
    "material_win2", &TRIANGLE_SHADER_TAG};

// Object tags (2D for window 1, 3D for window 2)
static constexpr device::ObjectTag OBJECT_WIN1_TAG{
    "object_win1", &MATERIAL_WIN1_TAG, 2};
static constexpr device::ObjectTag OBJECT_WIN2_TAG{
    "object_win2", &MATERIAL_WIN2_TAG, 3};

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

  std::print("\n=== Starting Hot Reload Loop ===\n");

  auto funcReloadCheck = [&core]() {
    // Check for library changes and reload if needed
    if (core.checkAndReloadIfNeeded()) {
      std::print(">>> Core library reloaded! <<<\n\n");
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
  gpuConfig.enableMultiGPU = false;
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

  // Configure swapchain
  window::SwapchainConfig swapConf;
  swapConf.vsync = core::Config::instance().getLoopConfig().enableVSync;

  // Window config with rendering chain initialization
  window::WindowConfig wConf;
  wConf.mainGPU = &dMan.getPrimaryDevice();
  wConf.secondaryGPUs = secondaryGPUs;
  wConf.vulkanInstance = &inst.getRaiiInstance();
  wConf.allocator = &vMan.getPrimaryAllocator();
  wConf.swapchainConfig = swapConf;

  // Create window 1
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

  // Initialize shader manager
  device::ShaderManager sMan;
  sMan.initialize(dMan.getPrimaryDevice());

  // Acquire the triangle shader program using the tag system
  // The shader is compiled when acquired, and shared across materials
  device::ShaderProgram *triangleProgram = sMan.acquire(&TRIANGLE_SHADER_TAG);
  if (triangleProgram && triangleProgram->compiled) {
    std::print("[Main] Triangle shader program acquired successfully\n");
  } else {
    std::print(stderr, "[Main] Failed to acquire triangle shader program\n");
  }

  // Create materials using the tag system
  // Both materials share the same shader (saving memory) but are independent
  auto material1 = std::make_unique<device::Material>(MATERIAL_WIN1_TAG);
  auto material2 = std::make_unique<device::Material>(MATERIAL_WIN2_TAG);

  // Initialize materials if renderers are available
  if (win1 && win1->hasRenderer()) {
    device::PipelineConfig pConfig1;
    pConfig1.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig1.cullMode = vk::CullModeFlagBits::eNone;
    pConfig1.depthTestEnable = false;

    if (material1->initialize(sMan, dMan.getPrimaryDevice(),
                              win1->getRenderer()->getRenderPass(), pConfig1)) {
      std::print("[Main] Material 1 initialized\n");
    }
  }

  if (win2 && win2->hasRenderer()) {
    device::PipelineConfig pConfig2;
    pConfig2.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig2.cullMode = vk::CullModeFlagBits::eNone;
    pConfig2.depthTestEnable = false;

    if (material2->initialize(sMan, dMan.getPrimaryDevice(),
                              win2->getRenderer()->getRenderPass(), pConfig2)) {
      std::print("[Main] Material 2 initialized\n");
    }
  }

  // Create objects using the tag system
  // Window 1: 2D object (rotation only around Z axis)
  auto object1 = std::make_unique<device::Object2D>(OBJECT_WIN1_TAG);
  device::Transform2D t2d;
  t2d.positionX = 0.0f;
  t2d.positionY = 0.0f;
  t2d.rotation = 0.0f;
  object1->setTransform(t2d);

  // Window 2: 3D object (full 3D rotation)
  auto object2 = std::make_unique<device::Object3D>(OBJECT_WIN2_TAG);
  device::Transform3D t3d;
  t3d.positionX = 0.0f;
  t3d.positionY = 0.0f;
  t3d.positionZ = 0.0f;
  object2->setTransform(t3d);

  // Initialize object descriptor sets and uniform buffers
  if (win1 && material1->isInitialized()) {
    object1->initialize(vMan.getPrimaryAllocator(), dMan.getPrimaryDevice(),
                        material1->getDescriptorSetLayout(),
                        window::MAX_FRAMES_IN_FLIGHT);
  }

  if (win2 && material2->isInitialized()) {
    object2->initialize(vMan.getPrimaryAllocator(), dMan.getPrimaryDevice(),
                        material2->getDescriptorSetLayout(),
                        window::MAX_FRAMES_IN_FLIGHT);
  }

  int frame = 0;
  while (!wMan.checkWindowsVectorEmpty()) { // TODO improve closing function
    std::print("Frame {}\n", frame);
    std::print("\n");

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
      wMan.destroyWindow(win);
    }

    if (wMan.checkWindowsVectorEmpty()) {
      break;
    }

    // Update object transforms (animate)
    if (object1->isInitialized()) {
      device::Transform2D t = object1->getTransform();
      t.rotation += 0.01f; // 2D rotation only around Z
      object1->setTransform(t);
      object1->updateUniforms(frame % window::MAX_FRAMES_IN_FLIGHT, nullptr,
                              nullptr);
    }

    if (object2->isInitialized()) {
      device::Transform3D t = object2->getTransform();
      t.rotationY += 0.02f; // 3D rotation around Y axis
      object2->setTransform(t);
      object2->updateUniforms(frame % window::MAX_FRAMES_IN_FLIGHT, nullptr,
                              nullptr);
    }

    frame++;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // Release resources in order
  object1->release();
  object2->release();
  material1->release();
  material2->release();
  sMan.release(&TRIANGLE_SHADER_TAG);

#ifdef ENGINE_DEBUG
  hotReload.request_stop();
  hotReload.join();
#endif

  return EXIT_SUCCESS;
}
