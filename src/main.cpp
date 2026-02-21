#include "material.h"
#include "object.h"
#include "renderer.h"
#include "scene.h"
#include "shader_manager.h"
#include "swapchain.h"
#include "thread_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"
#include "window.h"
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stop_token>
#ifdef ENGINE_DEBUG
#include "core_export_struct.h"
#include "hot_reload.h"
#endif
#include <print>
#include <thread>

// ============================================================================
// Static tags (must have static storage duration)
// ============================================================================

// Shader tag
static constexpr device::ShaderTag TRIANGLE_SHADER_TAG{
    "triangle", "triangle.slang", "vertMain", "fragMain", "geomMain"};

// Material tag (no dimension — that belongs to the object)
static constexpr window::MaterialTag TRIANGLE_MATERIAL_TAG{
    "triangle_material", &TRIANGLE_SHADER_TAG};

// Object tags: n-dimensional objects with n faces
// 2D triangle: 2 dimensions, 1 face (the triangle itself, 3 vertices)
static constexpr window::ObjectTag TRIANGLE_2D_TAG{
    "triangle_2d", &TRIANGLE_MATERIAL_TAG, 2, 1};
// 3D triangle: 3 dimensions, 1 face (3 vertices)
static constexpr window::ObjectTag TRIANGLE_3D_TAG{
    "triangle_3d", &TRIANGLE_MATERIAL_TAG, 3, 1};

// Scene tags
static constexpr window::SceneTag SCENE_2D_TAG{"scene_2d"};
static constexpr window::SceneTag SCENE_3D_TAG{"scene_3d"};

// ============================================================================
// Concrete Scene Implementation
// ============================================================================

/**
 * @brief A concrete scene that renders a triangle object
 *
 * Demonstrates the scene lifecycle: load creates GPU resources (material,
 * object with faces, pipeline), draw issues render commands per face,
 * unload frees GPU memory.
 */
class TriangleScene : public window::Scene {
public:
  TriangleScene(const window::SceneTag &sceneTag,
                const window::ObjectTag &objTag)
      : Scene(sceneTag), objTag_(objTag) {}

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override {
    // secondaryGPUs can be used for multi-GPU rendering workloads such as
    // offscreen rendering on secondary devices with result compositing on
    // the primary device. This basic scene uses the primary GPU only.
    (void)secondaryGPUs;

    // Create and initialize base material (acquires shader program)
    material_ = std::make_unique<window::Material>(*objTag_.baseMaterialTag);
    if (!material_->initialize(shaderManager)) {
      std::println(stderr, "[{}] Failed to initialize material", getName());
      return false;
    }

    // Create object with runtime dimension and face count from tag
    object_ = std::make_unique<window::Object>(objTag_);

    // Configure face 0: the triangle (3 vertices starting at offset 0)
    object_->setFaceVertices(0, 0, 3);

    // Initialize object (creates pipeline, descriptor sets, UBOs)
    window::PipelineConfig pConfig;
    pConfig.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig.cullMode = vk::CullModeFlagBits::eNone;
    pConfig.depthTestEnable = false;

    if (!object_->initialize(allocator, device, *material_,
                             renderer.getRenderPass(),
                             window::MAX_FRAMES_IN_FLIGHT, pConfig)) {
      std::println(stderr, "[{}] Failed to initialize object", getName());
      return false;
    }

    setLoaded(true);
    std::println("[{}] Scene loaded ({}D, {} faces)", getName(),
                 objTag_.dimension, objTag_.faceCount);
    return true;
  }

  void unload() override {
    if (object_) {
      object_->release();
      object_.reset();
    }
    if (material_) {
      material_->release();
      material_.reset();
    }
    setLoaded(false);
    std::println("[{}] Scene unloaded", getName());
  }

  void update(float /*deltaTime*/) override {
    // Update transforms, animations, etc.
  }

  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) override {
    if (object_ && object_->isInitialized()) {
      glm::mat4 view(1.0f);
      glm::mat4 proj(1.0f);
      object_->updateUniforms(frameIndex, view, proj);
      object_->draw(cmd, frameIndex);
    }
  }

private:
  const window::ObjectTag &objTag_;
  std::unique_ptr<window::Material> material_;
  std::unique_ptr<window::Object> object_;
};

// ============================================================================
// Main Application
// ============================================================================

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

  // Initialize shader manager
  device::ShaderManager sMan;
  sMan.initialize(dMan.getPrimaryDevice());

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

  // Window config with rendering chain and scene support
  window::WindowConfig wConf;
  wConf.mainGPU = &dMan.getPrimaryDevice();
  wConf.secondaryGPUs = secondaryGPUs;
  wConf.vulkanInstance = &inst.getRaiiInstance();
  wConf.allocator = &vMan.getPrimaryAllocator();
  wConf.shaderManager = &sMan;
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

  // Create scenes using the tag system and add them to windows
  // Window 1: 2D triangle scene
  if (win1 && win1->hasRenderer()) {
    auto scene2d =
        std::make_unique<TriangleScene>(SCENE_2D_TAG, TRIANGLE_2D_TAG);
    win1->addScene(&SCENE_2D_TAG, std::move(scene2d));
    win1->presentScene(&SCENE_2D_TAG);
  }

  // Window 2: 3D triangle scene
  if (win2 && win2->hasRenderer()) {
    auto scene3d =
        std::make_unique<TriangleScene>(SCENE_3D_TAG, TRIANGLE_3D_TAG);
    win2->addScene(&SCENE_3D_TAG, std::move(scene3d));
    win2->presentScene(&SCENE_3D_TAG);
  }

  // Main loop with scene-based rendering
  int frame = 0;
  while (!wMan.checkWindowsVectorEmpty()) {
    std::print("Frame {}\n", frame);
    std::print("\n");

    wMan.pollAllEvents();

    // Render active scenes on all windows
    for (const auto &win : wMan.getWindows()) {
      if (!win->shouldClose()) {
        win->renderFrame();
      }
    }

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

    frame++;
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

#ifdef ENGINE_DEBUG
  hotReload.request_stop();
  hotReload.join();
#endif

  return EXIT_SUCCESS;
}
