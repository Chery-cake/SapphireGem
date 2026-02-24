#include "material.h"
#include "object.h"
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
#include <memory>
#include <stop_token>
#ifdef ENGINE_DEBUG
#include "core_export_struct.h"
#include "hot_reload.h"
#endif
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include <print>
#include <thread>

// ============================================================================
// Static tags (must have static storage duration)
// ============================================================================

// Cube shader tag (vertex + fragment + geometry)
static constexpr device::ShaderTag CUBE_UNIFIED_SHADER_TAG{
    "cube_unified", "cube_unified.slang", "vertMain", "fragMain", "geomMain"};

// Cube material tag (single unified material for all faces)
static constexpr window::MaterialTag CUBE_UNIFIED_MATERIAL_TAG{
    "cube_unified_mat", &CUBE_UNIFIED_SHADER_TAG};

// 2D quad shader tag (vertex + fragment + geometry)
static constexpr device::ShaderTag QUAD_2D_SHADER_TAG{
    "quad2d_unified", "quad2d_unified.slang", "vertMain", "fragMain",
    "geomMain"};

// 2D quad material tag
static constexpr window::MaterialTag QUAD_2D_MATERIAL_TAG{"quad2d_unified_mat",
                                                          &QUAD_2D_SHADER_TAG};

// 2D quad object tag
static constexpr window::ObjectTag QUAD_2D_OBJ_TAG{"quad2d_obj",
                                                   &QUAD_2D_MATERIAL_TAG};

// Texture tags for shared assets
// Note: ImageTag uses std::variant internally, so these cannot be constexpr
static const window::ImageTag CHECKERBOARD_IMAGE{window::ImageFromFile{
    "checkerboard", "assets/textures/checkerboard.png", 256, 256}};

// Atlas: loaded once, 6 regions reference it as layers
static const window::AtlasTag LAYER_ATLAS{
    "layer_atlas", "assets/textures/layer_atlas.png", 512, 512};

// Atlas grid layout: 3 columns × 2 rows
static constexpr uint32_t ATLAS_COLS = 3;
static constexpr uint32_t ATLAS_ROWS = 2;
static constexpr uint32_t ATLAS_CELL_W = 170;
static constexpr uint32_t ATLAS_CELL_H = 256;
static constexpr uint32_t ATLAS_LAST_COL_W = 172; // 512 - 2*170

// 6 atlas region images
static const window::ImageTag ATLAS_REGION_0{window::ImageFromAtlasRegion{
    "atlas_region_0", &LAYER_ATLAS, 0, 0, ATLAS_CELL_W, ATLAS_CELL_H}};
static const window::ImageTag ATLAS_REGION_1{
    window::ImageFromAtlasRegion{"atlas_region_1", &LAYER_ATLAS, ATLAS_CELL_W,
                                 0, ATLAS_CELL_W, ATLAS_CELL_H}};
static const window::ImageTag ATLAS_REGION_2{window::ImageFromAtlasRegion{
    "atlas_region_2", &LAYER_ATLAS, ATLAS_CELL_W * 2, 0, ATLAS_LAST_COL_W,
    ATLAS_CELL_H}};
static const window::ImageTag ATLAS_REGION_3{
    window::ImageFromAtlasRegion{"atlas_region_3", &LAYER_ATLAS, 0,
                                 ATLAS_CELL_H, ATLAS_CELL_W, ATLAS_CELL_H}};
static const window::ImageTag ATLAS_REGION_4{
    window::ImageFromAtlasRegion{"atlas_region_4", &LAYER_ATLAS, ATLAS_CELL_W,
                                 ATLAS_CELL_H, ATLAS_CELL_W, ATLAS_CELL_H}};
static const window::ImageTag ATLAS_REGION_5{window::ImageFromAtlasRegion{
    "atlas_region_5", &LAYER_ATLAS, ATLAS_CELL_W * 2, ATLAS_CELL_H,
    ATLAS_LAST_COL_W, ATLAS_CELL_H}};

static const window::TextureLayerInfo CHECKERBOARD_LAYERS[] = {
    {&CHECKERBOARD_IMAGE}};
static const window::TextureLayerInfo ATLAS_LAYERS[] = {
    {&ATLAS_REGION_0}, {&ATLAS_REGION_1}, {&ATLAS_REGION_2},
    {&ATLAS_REGION_3}, {&ATLAS_REGION_4}, {&ATLAS_REGION_5}};
// TODO discover why if ATLAS_REGION_0 is added at index 0 of CHECKERBOAR_LAYERS
// then the ATLAS_LAYERS start rendering the layers properly. and why the
// program doesn't start if all the ATLAS_REGION_* aren't in use

static const window::TextureTag CHECKERBOARD_TEX_TAG{"checkerboard_tex",
                                                     CHECKERBOARD_LAYERS, 1};
static const window::TextureTag ATLAS_TEX_TAG{"layer_atlas_tex", ATLAS_LAYERS,
                                              6};

// Cube object tag (uses the unified material)
static constexpr window::ObjectTag CUBE_OBJ_TAG{"cube_obj",
                                                &CUBE_UNIFIED_MATERIAL_TAG};

// Scene tags
static constexpr window::SceneTag SCENE_CUBE_TAG{"scene_cube"};
static constexpr window::SceneTag SCENE_2D_TAG{"scene_2d"};

// ============================================================================
// Concrete Scene Implementations
// ============================================================================

class CubeScene3D : public window::Scene {
private:
  // Single unified material for all cube faces
  std::unique_ptr<window::Material> material_;

  std::unique_ptr<window::Object<3>> cube_;
  float totalTime_ = 0.0f;

  // Shared textures (shared_ptr for memory-efficient sharing across objects)
  std::shared_ptr<window::Texture> checkerboardTex_;
  std::shared_ptr<window::Texture> atlasLayerTex_;

public:
  explicit CubeScene3D(const window::SceneTag &sceneTag,
                       std::shared_ptr<window::Texture> checkerboard,
                       std::shared_ptr<window::Texture> atlas)
      : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
        atlasLayerTex_(std::move(atlas)) {}

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override {
    (void)secondaryGPUs;

    // Initialize the single unified material
    material_ = std::make_unique<window::Material>(CUBE_UNIFIED_MATERIAL_TAG);

    if (!material_->initialize(shaderManager)) {
      std::println(stderr, "[{}] Failed to initialize cube material",
                   getName());
      return false;
    }

    // Upload shared textures if not already uploaded
    if (checkerboardTex_ && !checkerboardTex_->isUploaded()) {
      if (!checkerboardTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload checkerboard texture",
                     getName());
        return false;
      }
      checkerboardTex_->createSampler(device);
    }

    if (atlasLayerTex_ && !atlasLayerTex_->isUploaded()) {
      if (!atlasLayerTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload atlas layer texture",
                     getName());
        return false;
      }
      atlasLayerTex_->createSampler(device);
    }

    // Cube: 36 vertices (geometry generated in shader via SV_VertexID)
    std::vector<window::Vertex<3>> vertices(36);
    for (auto &v : vertices) {
      v.position = {0.0f, 0.0f, 0.0f};
      v.color = {1.0f, 1.0f, 1.0f};
    }

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t i = 0; i < 36; ++i) {
      indices.push_back(i);
    }

    cube_ = std::make_unique<window::Object<3>>(
        CUBE_OBJ_TAG, std::move(vertices), std::move(indices));

    // Assign shared textures to cube faces (triangle indices)
    // Front face (+Z) = triangles 0,1 (mode 1): checkerboard texture
    cube_->setFaceTexture(0, checkerboardTex_);
    cube_->setFaceTexture(1, checkerboardTex_);
    // Back face (-Z) = triangles 2,3 (mode 2): atlas layer texture
    cube_->setFaceTexture(2, atlasLayerTex_);
    cube_->setFaceTexture(3, atlasLayerTex_);
    // Left/Right faces (mode 3): gradient + wave (procedural, no texture)
    // Top/Bottom faces (mode 0): plain color (procedural, no texture)

    // Pipeline config with push constants for time animation
    window::PipelineConfig pConfig;
    pConfig.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig.cullMode = vk::CullModeFlagBits::eBack;
    pConfig.frontFace = vk::FrontFace::eCounterClockwise;
    pConfig.depthTestEnable = true;
    pConfig.depthWriteEnable = true;
    pConfig.pushConstantSize = sizeof(float); // time
    pConfig.pushConstantStages = vk::ShaderStageFlagBits::eVertex;

    if (!cube_->initialize(allocator, device, *material_,
                           renderer.getRenderPass(),
                           window::MAX_FRAMES_IN_FLIGHT, pConfig)) {
      std::println(stderr, "[{}] Failed to initialize cube", getName());
      return false;
    }

    setLoaded(true);
    std::println("[{}] Cube scene loaded ({} faces, {} textures, "
                 "1 unified material)",
                 getName(), cube_->getFaceCount(),
                 cube_->getTextureSlotCount());
    return true;
  }

  void unload() override {
    if (cube_) {
      cube_->release();
      cube_.reset();
    }
    if (material_) {
      material_->release();
      material_.reset();
    }
    setLoaded(false);
    // Shared textures are not released here - they may be used by other
    // objects
    std::println("[{}] Cube scene unloaded", getName());
  }

  void update(float deltaTime) override { totalTime_ += deltaTime; }

  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) override {
    if (cube_ && cube_->isInitialized()) {
      // Update rotation: spin the cube around Y and X axes
      float rotY = totalTime_ * 0.5f;
      float rotX = totalTime_ * 0.3f;
      cube_->setRotation({rotX, rotY, 0.0f});
      cube_->setTime(totalTime_);

      // Set up 3D view and projection matrices
      glm::mat4 view =
          glm::lookAt(glm::vec3(0.0f, 0.5f, 2.5f), glm::vec3(0.0f, 0.0f, 0.0f),
                      glm::vec3(0.0f, 1.0f, 0.0f));

      // Perspective projection (Vulkan clip space: Y inverted)
      glm::mat4 proj =
          glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
      proj[1][1] *= -1.0f; // Flip Y for Vulkan

      cube_->updateUniforms(frameIndex, view, proj);
      cube_->draw(cmd, frameIndex);
    }
  }
};

class Quad2DScene : public window::Scene {
private:
  std::unique_ptr<window::Material> material_;

  // Shared texture (same checkerboard used by the cube - memory efficient)
  std::shared_ptr<window::Texture> checkerboardTex_;

  std::unique_ptr<window::Object<2>> quad_;
  float totalTime_ = 0.0f;

public:
  explicit Quad2DScene(const window::SceneTag &sceneTag,
                       std::shared_ptr<window::Texture> checkerboard)
      : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)) {}

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override {
    (void)secondaryGPUs;

    material_ = std::make_unique<window::Material>(QUAD_2D_MATERIAL_TAG);
    if (!material_->initialize(shaderManager)) {
      std::println(stderr, "[{}] Failed to initialize 2D material", getName());
      return false;
    }

    // Upload shared texture if not already uploaded (shared with cube
    // scene)
    if (checkerboardTex_ && !checkerboardTex_->isUploaded()) {
      if (!checkerboardTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload checkerboard texture",
                     getName());
        return false;
      }
      checkerboardTex_->createSampler(device);
    }

    // 2D quad: 4 faces × 2 triangles × 3 vertices = 24 vertices
    // Each face has a different rendering mode
    std::vector<window::Vertex<2>> vertices(24);
    for (auto &v : vertices) {
      v.position = {0.0f, 0.0f};
      v.color = {1.0f, 1.0f, 1.0f};
    }

    std::vector<uint32_t> indices;
    indices.reserve(24);
    for (uint32_t i = 0; i < 24; ++i) {
      indices.push_back(i);
    }

    quad_ = std::make_unique<window::Object<2>>(
        QUAD_2D_OBJ_TAG, std::move(vertices), std::move(indices));

    // Assign checkerboard texture to face 1 (top-right quadrant)
    // Triangles 2,3 use render mode 1 (checkerboard texture)
    quad_->setFaceTexture(2, checkerboardTex_);
    quad_->setFaceTexture(3, checkerboardTex_);
    // Face 0 (triangles 0,1): plain color (mode 0, no texture)
    // Face 2 (triangles 4,5): gradient (mode 2, procedural)
    // Face 3 (triangles 6,7): wave (mode 3, procedural)

    window::PipelineConfig pConfig;
    pConfig.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig.cullMode = vk::CullModeFlagBits::eNone;
    pConfig.depthTestEnable = false;
    pConfig.pushConstantSize = sizeof(float); // time
    pConfig.pushConstantStages =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    if (!quad_->initialize(allocator, device, *material_,
                           renderer.getRenderPass(),
                           window::MAX_FRAMES_IN_FLIGHT, pConfig)) {
      std::println(stderr, "[{}] Failed to initialize 2D quad", getName());
      return false;
    }

    setLoaded(true);
    std::println("[{}] 2D scene loaded ({} faces, {} textures)", getName(),
                 quad_->getFaceCount(), quad_->getTextureSlotCount());
    return true;
  }

  void unload() override {
    if (quad_) {
      quad_->release();
      quad_.reset();
    }
    if (material_) {
      material_->release();
      material_.reset();
    }
    setLoaded(false);
    std::println("[{}] 2D scene unloaded", getName());
  }

  void update(float deltaTime) override { totalTime_ += deltaTime; }

  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) override {
    if (quad_ && quad_->isInitialized()) {
      quad_->setTime(totalTime_);

      // 2D uses 3×3 matrices (identity for now)
      glm::mat3 view(1.0f);
      glm::mat3 proj(1.0f);
      quad_->updateUniforms(frameIndex, view, proj);
      quad_->draw(cmd, frameIndex);
    }
  }
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

  // Create shared textures (shared_ptr ensures memory-efficient sharing)
  auto checkerboardTex =
      std::make_shared<window::Texture>(CHECKERBOARD_TEX_TAG);
  auto atlasLayerTex = std::make_shared<window::Texture>(ATLAS_TEX_TAG);

  // Create scenes using the tag system and add them to windows
  // Window 1: 3D cube scene with per-face textures (Object<3>)
  if (win1 && win1->hasRenderer()) {
    auto sceneCube = std::make_unique<CubeScene3D>(
        SCENE_CUBE_TAG, checkerboardTex, atlasLayerTex);
    win1->addScene(&SCENE_CUBE_TAG, std::move(sceneCube));
    win1->presentScene(&SCENE_CUBE_TAG);
  }

  // Window 2: 2D quad scene with multiple render modes (Object<2>)
  // Shares the checkerboard texture with the cube to save GPU memory
  if (win2 && win2->hasRenderer()) {
    auto scene2d = std::make_unique<Quad2DScene>(SCENE_2D_TAG, checkerboardTex);
    win2->addScene(&SCENE_2D_TAG, std::move(scene2d));
    win2->presentScene(&SCENE_2D_TAG);
  }

  // int frame = 0;
  auto lastTime = std::chrono::steady_clock::now();
  while (!wMan.checkWindowsVectorEmpty()) {
    // std::print("Frame {}\n", frame);
    // std::print("\n");

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

    // frame++;
    std::this_thread::sleep_for(std::chrono::nanoseconds(100));
  }

#ifdef ENGINE_DEBUG
  hotReload.request_stop();
  hotReload.join();
#endif

  return EXIT_SUCCESS;
}
