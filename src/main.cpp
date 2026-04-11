#include "config.h"
#include "config_threads.h"
#include "config_vulkan.h"
#include "hot_reload_modules.h"
#include "image_array_registry.h"
#include "material.h"
#include "memory_allocator.h"
#include "renderer.h"
#include "shader_manager.h"
#include "swapchain.h"
#include "texture_table.h"
#include "thread_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"
#include "window.h"
#include <chrono>
#include <cstdlib>
#include <memory>
#include <print>
#include <thread>

// Scene implementations (modularised into separate files)
#include "scenes/cube_scene.h"
#include "scenes/quad2d_scene.h"
#include "scenes/polytope_scene.h"

// ============================================================================
// Static tags (must have static storage duration)
// ============================================================================

// Unified object shader tag (vertex + fragment + geometry)
static constexpr device::ShaderTag OBJECT_BASE_SHADER_TAG{
    "object_base", "object_base.slang", "vertMain", "fragMain", "geomMain"};

// Material tags – all use the unified object_base shader
// constexpr ensures constant (compile-time) initialization; external linkage
// is inherited from the extern declarations in the scene headers.
constexpr window::MaterialTag CUBE_MATERIAL_TAG{
    "cube_bindless_mat", &OBJECT_BASE_SHADER_TAG};
constexpr window::MaterialTag QUAD_2D_MATERIAL_TAG{
    "quad2d_bindless_mat", &OBJECT_BASE_SHADER_TAG};
constexpr window::MaterialTag POLYTOPE_MATERIAL_TAG{
    "polytope_mat", &OBJECT_BASE_SHADER_TAG};

// Object tags
constexpr window::ObjectTag CUBE_OBJ_TAG{"cube_obj", &CUBE_MATERIAL_TAG};
constexpr window::ObjectTag QUAD_2D_OBJ_TAG{"quad2d_obj",
                                             &QUAD_2D_MATERIAL_TAG};
constexpr window::ObjectTag POLYTOPE_OBJ_TAG{"polytope_obj",
                                              &POLYTOPE_MATERIAL_TAG};

// Texture tags for shared assets (still used for CPU-side image loading)
static const window::ImageTag CHECKERBOARD_IMAGE{window::ImageFromFile{
    "checkerboard", "assets/textures/checkerboard.png", 256, 256}};

static const window::TextureLayerInfo CHECKERBOARD_LAYERS[] = {
    {&CHECKERBOARD_IMAGE}};
static const window::TextureTag CHECKERBOARD_TEX_TAG{"checkerboard_tex",
                                                     CHECKERBOARD_LAYERS, 1};

// Atlas texture: layer_atlas.png (512x512, contains shapes in a 2x2 grid)
static const window::ImageTag LAYER_ATLAS_IMAGE{window::ImageFromFile{
    "layer_atlas", "assets/textures/layer_atlas.png", 512, 512}};

static const window::TextureLayerInfo LAYER_ATLAS_LAYERS[] = {
    {&LAYER_ATLAS_IMAGE}};
static const window::TextureTag LAYER_ATLAS_TEX_TAG{"layer_atlas_tex",
                                                    LAYER_ATLAS_LAYERS, 1};

// Scene tags
static constexpr window::SceneTag SCENE_CUBE_TAG{"scene_cube"};
static constexpr window::SceneTag SCENE_2D_TAG{"scene_2d"};
static constexpr window::SceneTag SCENE_POLYTOPE_TAG{"scene_polytope"};

// ============================================================================
// Main Application
// ============================================================================

int main(int argc, char *argv[]) {

  std::print("argc: {}\n", argc);

  for (int i = 0; i < argc; i++) {
    std::print("argv[{}]: {}\n", i, argv[i]);
  }

  // Initialize hot reload module management
  ModuleReloadManager reloadManager;
  {
    std::string exe_path = argv[0];
    std::string exe_dir = exe_path.substr(0, exe_path.find_last_of("/\\"));
    if (!reloadManager.initialize(exe_dir)) {
      return EXIT_FAILURE;
    }
  }
  reloadManager.startMonitoring();

  // Configure multi-GPU if available
  core::GPUConfig gpuConfig;
  gpuConfig.enableMultiGPU = false;
  gpuConfig.gpuCount = 0; // Auto-detect
  core::Config::instance().getThreadsConfig().setGPUConfig(gpuConfig);

  device::VulkanInstance inst;
  inst.initialize();

  device::DeviceManager dMan;
  device::VulkanDeviceConfig devConfig;
  devConfig.surface = nullptr;
  devConfig.enableMultiGPU =
      core::Config::instance().getThreadsConfig().getGPUConfig().enableMultiGPU;
  devConfig.preferredGPUIndex = core::Config::instance()
                                    .getThreadsConfig()
                                    .getGPUConfig()
                                    .preferredGPUIndex;
  dMan.initialize(inst, devConfig);

  // Update GPU count in config based on actual device count
  gpuConfig.gpuCount = static_cast<uint32_t>(dMan.getDeviceCount());
  core::Config::instance().getThreadsConfig().setGPUConfig(gpuConfig);

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
  swapConf.vsync =
      core::Config::instance().getThreadsConfig().getLoopConfig().enableVSync;

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

  // Create Window 3 (polytope demo)
  wConf.title = "SapphireEngine - Window 3";
  window::Window *win3 = wMan.createWindow(wConf);

  // Update loop config to match window count
  {
    core::LoopConfig loopCfg =
        core::Config::instance().getThreadsConfig().getLoopConfig();
    loopCfg.mainLoopCount = static_cast<uint32_t>(wMan.getWindowCount());
    core::Config::instance().getThreadsConfig().setLoopConfig(loopCfg);
  }

  // Initialize thread pools based on effective thread allocation
  {
    auto &tm = core::ThreadManager::instance();
    auto effectiveAlloc = core::Config::instance()
                              .getThreadsConfig()
                              .getEffectiveThreadAllocation();

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

  // Create shared textures (shared_ptr ensures memory-efficient sharing)
  auto checkerboardTex =
      std::make_shared<window::Texture>(CHECKERBOARD_TEX_TAG);
  auto layerAtlasTex = std::make_shared<window::Texture>(LAYER_ATLAS_TEX_TAG);

  // Create a shared image registry per-device so all scenes/windows
  // share the same image buffer and don't duplicate images in GPU memory
  auto sharedImageRegistry = std::make_shared<device::ImageArrayRegistry>();

  // Create a shared texture table so all scenes contribute to the same
  // TextureRecord/TextureLayer SSBOs and descriptor bindings stay consistent
  auto sharedTextureTable = std::make_shared<device::TextureTableManager>();

  // Create scenes using the tag system and add them to windows
  // Window 1: 3D cube scene with bindless textures (Object<3>)
  if (win1 && win1->hasRenderer()) {
    auto sceneCube = std::make_unique<CubeScene3D>(
        SCENE_CUBE_TAG, checkerboardTex, layerAtlasTex, sharedImageRegistry,
        sharedTextureTable);
    win1->addScene(&SCENE_CUBE_TAG, std::move(sceneCube));
    win1->presentScene(&SCENE_CUBE_TAG);
  }

  // Window 2: 2D quad scene with bindless textures (Object<2>)
  // Shares the checkerboard texture, image registry and texture table
  if (win2 && win2->hasRenderer()) {
    auto scene2d = std::make_unique<Quad2DScene>(
        SCENE_2D_TAG, checkerboardTex, sharedImageRegistry, sharedTextureTable);
    win2->addScene(&SCENE_2D_TAG, std::move(scene2d));
    win2->presentScene(&SCENE_2D_TAG);
  }

  // Window 3: Random polytope demo scene (Object<3>)
  if (win3) {
    win3->setEventCallback(
        [](const window::WindowEvent &) {
          std::print("[Main] Window 3 close requested\n");
        },
        window::WindowEventType::Close);

    win3->setEventCallback(
        [](const window::WindowEvent &event) {
          std::print("[Main] Window 3 resized to {}x{}\n", event.width,
                     event.height);
        },
        window::WindowEventType::Resize);

    if (win3->hasRenderer()) {
      auto scenePolytope = std::make_unique<PolytopeDemoScene>(
          SCENE_POLYTOPE_TAG, checkerboardTex, layerAtlasTex,
          sharedImageRegistry, sharedTextureTable);
      win3->addScene(&SCENE_POLYTOPE_TAG, std::move(scenePolytope));
      win3->presentScene(&SCENE_POLYTOPE_TAG);
    }
  }

  // TODO improve frame cap
  int frameTarget = 60;
  float deltaTimeTarget = 1.0f / frameTarget;
  auto lastTime = std::chrono::steady_clock::now();
  while (!wMan.checkWindowsVectorEmpty()) {
    auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    wMan.pollAllEvents();

    // Render active scenes on all windows
    for (const auto &win : wMan.getWindows()) {
      if (!win->shouldClose()) {
        win->renderFrame(deltaTime);
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

    auto end = std::chrono::steady_clock::now();
    float frameDuration = std::chrono::duration<float>(end - lastTime).count();
    if (frameDuration < deltaTimeTarget) {
      auto sleepDuration =
          std::chrono::duration<float>(deltaTimeTarget - frameDuration);
      std::this_thread::sleep_for(sleepDuration);
    }
  }

  reloadManager.shutdown();

  return EXIT_SUCCESS;
}
