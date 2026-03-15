#include "config.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "hot_reload_modules.h"
#include "image_array_registry.h"
#include "material.h"
#include "object.h"
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

// ============================================================================
// Static tags (must have static storage duration)
// ============================================================================

// Bindless cube shader tag (vertex + fragment + geometry)
static constexpr device::ShaderTag CUBE_BINDLESS_SHADER_TAG{
    "cube_bindless", "cube_bindless.slang", "vertMain", "fragMain", "geomMain"};

// Bindless 2D quad shader tag (vertex + fragment + geometry)
static constexpr device::ShaderTag QUAD_2D_BINDLESS_SHADER_TAG{
    "quad2d_bindless", "quad2d_bindless.slang", "vertMain", "fragMain",
    "geomMain"};

// Material tags (no texture binding plan — textures accessed via bindless)
static constexpr window::MaterialTag CUBE_MATERIAL_TAG{
    "cube_bindless_mat", &CUBE_BINDLESS_SHADER_TAG};
static constexpr window::MaterialTag QUAD_2D_MATERIAL_TAG{
    "quad2d_bindless_mat", &QUAD_2D_BINDLESS_SHADER_TAG};

// Object tags
static constexpr window::ObjectTag CUBE_OBJ_TAG{"cube_obj", &CUBE_MATERIAL_TAG};
static constexpr window::ObjectTag QUAD_2D_OBJ_TAG{"quad2d_obj",
                                                   &QUAD_2D_MATERIAL_TAG};

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

// ============================================================================
// Concrete Scene Implementations
// ============================================================================

class CubeScene3D : public window::Scene {
private:
  std::unique_ptr<window::Material> material_;
  std::unique_ptr<window::Object<3>> cube_;
  float totalTime_ = 0.0f;

  // Shared textures (for CPU-side image loading/upload)
  std::shared_ptr<window::Texture> checkerboardTex_;
  std::shared_ptr<window::Texture> layerAtlasTex_;

  // Shared bindless image registry (per-device, shared across scenes)
  std::shared_ptr<device::ImageArrayRegistry> imageRegistry_;
  std::shared_ptr<device::TextureTableManager> textureTable_;
  device::TextureId cubeTextureId_;
  device::TextureId atlasTextureId_;

public:
  explicit CubeScene3D(
      const window::SceneTag &sceneTag,
      std::shared_ptr<window::Texture> checkerboard,
      std::shared_ptr<window::Texture> layerAtlas,
      std::shared_ptr<device::ImageArrayRegistry> registry,
      std::shared_ptr<device::TextureTableManager> textureTable)
      : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
        layerAtlasTex_(std::move(layerAtlas)),
        imageRegistry_(std::move(registry)),
        textureTable_(std::move(textureTable)) {}

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override {
    (void)secondaryGPUs;

    // Initialize shared image registry if not already initialized
    if (!imageRegistry_->isInitialized()) {
      if (!imageRegistry_->initialize(device)) {
        std::println(stderr, "[{}] Failed to initialize image registry",
                     getName());
        return false;
      }
    }

    // Initialize the material (compiles bindless shaders)
    material_ = std::make_unique<window::Material>(CUBE_MATERIAL_TAG);
    if (!material_->initialize(shaderManager)) {
      std::println(stderr, "[{}] Failed to initialize cube material",
                   getName());
      return false;
    }

    // Upload checkerboard texture to GPU
    if (checkerboardTex_ && !checkerboardTex_->isUploaded()) {
      if (!checkerboardTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload checkerboard texture",
                     getName());
        return false;
      }
      checkerboardTex_->createSampler(device);
    }

    // Upload layer atlas texture to GPU
    if (layerAtlasTex_ && !layerAtlasTex_->isUploaded()) {
      if (!layerAtlasTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload layer atlas texture",
                     getName());
        return false;
      }
      layerAtlasTex_->createSampler(device);
    }

    // Register the uploaded checkerboard image in the shared bindless
    // registry
    device::ImageHandle checkerHandle;
    if (checkerboardTex_ && checkerboardTex_->isUploaded()) {
      const auto *layer = checkerboardTex_->getLayer(0);
      if (layer && layer->loaded) {
        checkerHandle = imageRegistry_->registerImage(
            device::ImageKind::eImage2D, layer->gpuImage.getView());
      }
    }

    // Register the atlas image in the shared bindless registry
    device::ImageHandle atlasHandle;
    if (layerAtlasTex_ && layerAtlasTex_->isUploaded()) {
      const auto *layer = layerAtlasTex_->getLayer(0);
      if (layer && layer->loaded) {
        atlasHandle = imageRegistry_->registerImage(device::ImageKind::eAtlas,
                                                    layer->gpuImage.getView());
      }
    }

    // Build texture record 0: one layer (checkerboard image)
    cubeTextureId_ = textureTable_->addRecord(1);
    {
      device::GPUTextureLayer cubeLayer;
      cubeLayer.image2DIndex = checkerHandle.isValid()
                                   ? static_cast<int32_t>(checkerHandle.index)
                                   : -1;
      textureTable_->setLayers(cubeTextureId_, {cubeLayer});
    }

    // Build texture record 1: 6 layers from the atlas (2×3 grid in a
    // 512×512 atlas). Each layer references the same atlas image but
    // with different UV sub-region offsets. Layers are composited with
    // individual transforms (tint, rotation, scale, blend mode).
    atlasTextureId_ = textureTable_->addRecord(6);
    {
      int32_t atlasIdx =
          atlasHandle.isValid() ? static_cast<int32_t>(atlasHandle.index) : -1;

      const float colScale = 0.5f;        // 2 columns
      const float rowScale = 1.0f / 3.0f; // 3 rows

      // Layer 0: top-left (0,0) — base layer, alpha blend
      device::GPUTextureLayer layer0;
      layer0.atlasIndex = atlasIdx;
      layer0.atlasUvOffsetX = 0.0f;
      layer0.atlasUvOffsetY = 0.0f;
      layer0.atlasUvScaleX = colScale;
      layer0.atlasUvScaleY = rowScale;
      layer0.blendMode = 0; // alpha blend

      // Layer 1: top-right (1,0) — additive, cool tint
      device::GPUTextureLayer layer1;
      layer1.atlasIndex = atlasIdx;
      layer1.atlasUvOffsetX = 0.5f;
      layer1.atlasUvOffsetY = 0.0f;
      layer1.atlasUvScaleX = colScale;
      layer1.atlasUvScaleY = rowScale;
      layer1.blendMode = 1; // additive
      layer1.tintR = 0.8f;
      layer1.tintG = 0.9f;
      layer1.tintB = 1.0f;

      // Layer 2: middle-left (0,1) — additive, warm tint
      device::GPUTextureLayer layer2;
      layer2.atlasIndex = atlasIdx;
      layer2.atlasUvOffsetX = 0.0f;
      layer2.atlasUvOffsetY = rowScale;
      layer2.atlasUvScaleX = colScale;
      layer2.atlasUvScaleY = rowScale;
      layer2.blendMode = 1; // additive
      layer2.tintR = 1.0f;
      layer2.tintG = 0.8f;
      layer2.tintB = 0.8f;

      // Layer 3: middle-right (1,1) — additive, slight rotation
      device::GPUTextureLayer layer3;
      layer3.atlasIndex = atlasIdx;
      layer3.atlasUvOffsetX = 0.5f;
      layer3.atlasUvOffsetY = rowScale;
      layer3.atlasUvScaleX = colScale;
      layer3.atlasUvScaleY = rowScale;
      layer3.blendMode = 1; // additive
      layer3.rotation = 0.2f;

      // Layer 4: bottom-left (0,2) — additive, scaled down
      device::GPUTextureLayer layer4;
      layer4.atlasIndex = atlasIdx;
      layer4.atlasUvOffsetX = 0.0f;
      layer4.atlasUvOffsetY = 2.0f * rowScale;
      layer4.atlasUvScaleX = colScale;
      layer4.atlasUvScaleY = rowScale;
      layer4.blendMode = 1; // additive
      layer4.scaleX = 0.8f;
      layer4.scaleY = 0.8f;

      // Layer 5: bottom-right (1,2) — additive, green tint
      device::GPUTextureLayer layer5;
      layer5.atlasIndex = atlasIdx;
      layer5.atlasUvOffsetX = 0.5f;
      layer5.atlasUvOffsetY = 2.0f * rowScale;
      layer5.atlasUvScaleX = colScale;
      layer5.atlasUvScaleY = rowScale;
      layer5.blendMode = 1; // additive
      layer5.tintR = 0.9f;
      layer5.tintG = 1.0f;
      layer5.tintB = 0.8f;

      textureTable_->setLayers(
          atlasTextureId_, {layer0, layer1, layer2, layer3, layer4, layer5});
    }

    // Upload texture tables to GPU
    if (!textureTable_->uploadToGPU(allocator, device)) {
      std::println(stderr, "[{}] Failed to upload texture tables", getName());
      return false;
    }

    // Commit descriptors (images + SSBOs)
    if (checkerboardTex_ && checkerboardTex_->getSampler()) {
      imageRegistry_->commitDescriptors(device, checkerboardTex_->getSampler(),
                                        &textureTable_->getRecordBuffer(),
                                        &textureTable_->getLayerBuffer());
    }

    // Create cube geometry (36 vertices for 12 triangles)
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

    // Set the bindless texture IDs and descriptor set
    cube_->setTextureId(cubeTextureId_);
    cube_->setAtlasTextureId(atlasTextureId_);
    cube_->setBindlessDescriptorSet(imageRegistry_->getDescriptorSet());

    // Pipeline config with push constants for time + textureId +
    // atlasTextureId
    window::PipelineConfig pConfig;
    pConfig.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig.cullMode = vk::CullModeFlagBits::eBack;
    pConfig.frontFace = vk::FrontFace::eCounterClockwise;
    pConfig.depthTestEnable = true;
    pConfig.depthWriteEnable = true;
    pConfig.pushConstantSize = sizeof(
        device::BindlessPushConstants); // time + textureId + atlasTextureId
    pConfig.pushConstantStages =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    if (!cube_->initialize(allocator, device, *material_,
                           renderer.getRenderPass(),
                           window::MAX_FRAMES_IN_FLIGHT, pConfig,
                           imageRegistry_->getDescriptorSetLayout())) {
      std::println(stderr, "[{}] Failed to initialize cube", getName());
      return false;
    }

    setLoaded(true);
    std::println("[{}] Cube scene loaded (bindless, textureId={}, "
                 "atlasTextureId={})",
                 getName(), cubeTextureId_.index, atlasTextureId_.index);
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
    textureTable_.reset();
    setLoaded(false);
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
  std::unique_ptr<window::Object<2>> quad_;
  float totalTime_ = 0.0f;

  // Shared texture (for CPU-side image loading/upload)
  std::shared_ptr<window::Texture> checkerboardTex_;

  // Shared bindless image registry (per-device, shared across scenes)
  std::shared_ptr<device::ImageArrayRegistry> imageRegistry_;
  std::shared_ptr<device::TextureTableManager> textureTable_;
  device::TextureId quadTextureId_;

public:
  explicit Quad2DScene(
      const window::SceneTag &sceneTag,
      std::shared_ptr<window::Texture> checkerboard,
      std::shared_ptr<device::ImageArrayRegistry> registry,
      std::shared_ptr<device::TextureTableManager> textureTable)
      : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
        imageRegistry_(std::move(registry)),
        textureTable_(std::move(textureTable)) {}

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override {
    (void)secondaryGPUs;

    // Initialize shared image registry if not already initialized
    if (!imageRegistry_->isInitialized()) {
      if (!imageRegistry_->initialize(device)) {
        std::println(stderr, "[{}] Failed to initialize image registry",
                     getName());
        return false;
      }
    }

    material_ = std::make_unique<window::Material>(QUAD_2D_MATERIAL_TAG);
    if (!material_->initialize(shaderManager)) {
      std::println(stderr, "[{}] Failed to initialize 2D material", getName());
      return false;
    }

    // Upload shared texture if not already uploaded
    if (checkerboardTex_ && !checkerboardTex_->isUploaded()) {
      if (!checkerboardTex_->upload(allocator, device)) {
        std::println(stderr, "[{}] Failed to upload checkerboard texture",
                     getName());
        return false;
      }
      checkerboardTex_->createSampler(device);
    }

    // Register the uploaded image in the shared bindless registry
    device::ImageHandle checkerHandle;
    if (checkerboardTex_ && checkerboardTex_->isUploaded()) {
      const auto *layer = checkerboardTex_->getLayer(0);
      if (layer && layer->loaded) {
        checkerHandle = imageRegistry_->registerImage(
            device::ImageKind::eImage2D, layer->gpuImage.getView());
      }
    }

    // Build a texture record with one layer (checkerboard image)
    quadTextureId_ = textureTable_->addRecord(1);

    device::GPUTextureLayer quadLayer;
    quadLayer.image2DIndex = checkerHandle.isValid()
                                 ? static_cast<int32_t>(checkerHandle.index)
                                 : -1;
    textureTable_->setLayers(quadTextureId_, {quadLayer});

    // Upload texture tables to GPU
    if (!textureTable_->uploadToGPU(allocator, device)) {
      std::println(stderr, "[{}] Failed to upload texture tables", getName());
      return false;
    }

    // Commit descriptors
    if (checkerboardTex_ && checkerboardTex_->getSampler()) {
      imageRegistry_->commitDescriptors(device, checkerboardTex_->getSampler(),
                                        &textureTable_->getRecordBuffer(),
                                        &textureTable_->getLayerBuffer());
    }

    // 2D quad: 24 vertices (4 faces × 2 triangles × 3 vertices)
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

    // Set the bindless texture ID and descriptor set
    quad_->setTextureId(quadTextureId_);
    quad_->setBindlessDescriptorSet(imageRegistry_->getDescriptorSet());

    window::PipelineConfig pConfig;
    pConfig.topology = vk::PrimitiveTopology::eTriangleList;
    pConfig.cullMode = vk::CullModeFlagBits::eNone;
    pConfig.depthTestEnable = false;
    pConfig.pushConstantSize = sizeof(
        device::BindlessPushConstants); // time + textureId + atlasTextureId
    pConfig.pushConstantStages =
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

    if (!quad_->initialize(allocator, device, *material_,
                           renderer.getRenderPass(),
                           window::MAX_FRAMES_IN_FLIGHT, pConfig,
                           imageRegistry_->getDescriptorSetLayout())) {
      std::println(stderr, "[{}] Failed to initialize 2D quad", getName());
      return false;
    }

    setLoaded(true);
    std::println("[{}] 2D scene loaded (bindless, textureId={})", getName(),
                 quadTextureId_.index);
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
    textureTable_.reset();
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
