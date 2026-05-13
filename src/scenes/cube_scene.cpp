#include "cube_scene.h"
#include "bindless_types.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include "renderer.h"
#include "scene.h"
#include "vulkan/vulkan.hpp"
#include <memory>
#include <print>

CubeScene3D::CubeScene3D(
    const window::SceneTag &sceneTag,
    std::shared_ptr<window::Texture> checkerboard,
    std::shared_ptr<window::Texture> layerAtlas,
    std::shared_ptr<device::ImageArrayRegistry> registry,
    std::shared_ptr<device::TextureTableManager> textureTable)
    : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
      layerAtlasTex_(std::move(layerAtlas)),
      imageRegistry_(std::move(registry)),
      textureTable_(std::move(textureTable)) {}

bool CubeScene3D::load(device::GPUDevice &device,
                       std::vector<device::GPUDevice *> &secondaryGPUs,
                       device::VMAAllocator &allocator,
                       device::ShaderManager &shaderManager,
                       window::Renderer &renderer) {
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
    std::println(stderr, "[{}] Failed to initialize cube material", getName());
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

  // Register the uploaded checkerboard image in the shared bindless registry
  device::ImageHandle checkerHandle;
  if (checkerboardTex_ && checkerboardTex_->isUploaded()) {
    const auto *layer = checkerboardTex_->getLayer(0);
    if (layer && layer->loaded) {
      checkerHandle = imageRegistry_->registerImage(device::ImageKind::eImage2D,
                                                    layer->gpuImage.getView());
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

  // Build texture record 1: 6 layers from the atlas (2x3 grid in a
  // 512x512 atlas). Each layer references the same atlas image but
  // with different UV sub-region offsets.
  atlasTextureId_ = textureTable_->addRecord(6);
  {
    int32_t atlasIdx =
        atlasHandle.isValid() ? static_cast<int32_t>(atlasHandle.index) : -1;

    const float colScale = 0.5f;        // 2 columns
    const float rowScale = 1.0f / 3.0f; // 3 rows

    // Layer 0: top-left (0,0)
    device::GPUTextureLayer layer0;
    layer0.atlasIndex = atlasIdx;
    layer0.atlasUvOffsetX = 0.0f;
    layer0.atlasUvOffsetY = 0.0f;
    layer0.atlasUvScaleX = colScale;
    layer0.atlasUvScaleY = rowScale;
    layer0.blendMode = 0;

    // Layer 1: top-right (1,0)
    device::GPUTextureLayer layer1;
    layer1.atlasIndex = atlasIdx;
    layer1.atlasUvOffsetX = 0.5f;
    layer1.atlasUvOffsetY = 0.0f;
    layer1.atlasUvScaleX = colScale;
    layer1.atlasUvScaleY = rowScale;
    layer1.blendMode = 1;
    layer1.tintR = 0.8f;
    layer1.tintG = 0.9f;
    layer1.tintB = 1.0f;

    // Layer 2: middle-left (0,1)
    device::GPUTextureLayer layer2;
    layer2.atlasIndex = atlasIdx;
    layer2.atlasUvOffsetX = 0.0f;
    layer2.atlasUvOffsetY = rowScale;
    layer2.atlasUvScaleX = colScale;
    layer2.atlasUvScaleY = rowScale;
    layer2.blendMode = 1;
    layer2.tintR = 1.0f;
    layer2.tintG = 0.8f;
    layer2.tintB = 0.8f;

    // Layer 3: middle-right (1,1)
    device::GPUTextureLayer layer3;
    layer3.atlasIndex = atlasIdx;
    layer3.atlasUvOffsetX = 0.5f;
    layer3.atlasUvOffsetY = rowScale;
    layer3.atlasUvScaleX = colScale;
    layer3.atlasUvScaleY = rowScale;
    layer3.blendMode = 1;
    layer3.rotation = 0.2f;

    // Layer 4: bottom-left (0,2)
    device::GPUTextureLayer layer4;
    layer4.atlasIndex = atlasIdx;
    layer4.atlasUvOffsetX = 0.0f;
    layer4.atlasUvOffsetY = 2.0f * rowScale;
    layer4.atlasUvScaleX = colScale;
    layer4.atlasUvScaleY = rowScale;
    layer4.blendMode = 1;
    layer4.scaleX = 0.8f;
    layer4.scaleY = 0.8f;

    // Layer 5: bottom-right (1,2)
    device::GPUTextureLayer layer5;
    layer5.atlasIndex = atlasIdx;
    layer5.atlasUvOffsetX = 0.5f;
    layer5.atlasUvOffsetY = 2.0f * rowScale;
    layer5.atlasUvScaleX = colScale;
    layer5.atlasUvScaleY = rowScale;
    layer5.blendMode = 1;
    layer5.tintR = 0.9f;
    layer5.tintG = 1.0f;
    layer5.tintB = 0.8f;

    textureTable_->setLayers(atlasTextureId_,
                             {layer0, layer1, layer2, layer3, layer4, layer5});
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

  // Create cube geometry: 8 unique vertices + 36 indices (12 triangles)
  // Using indexed drawing eliminates duplicate vertex positions, allowing
  // the compute shader to correctly propagate wave displacement to all
  // faces sharing a vertex.
  static constexpr std::array<std::array<float, 3>, 8> cubePos = {{
      {-0.5f, -0.5f, 0.5f},
      {0.5f, -0.5f, 0.5f},
      {0.5f, 0.5f, 0.5f},
      {-0.5f, 0.5f, 0.5f},
      {-0.5f, -0.5f, -0.5f},
      {0.5f, -0.5f, -0.5f},
      {0.5f, 0.5f, -0.5f},
      {-0.5f, 0.5f, -0.5f},
  }};

  std::vector<uint32_t> indices = {
      0, 1, 2, 0, 2, 3, // Front  (+Z)
      5, 4, 7, 5, 7, 6, // Back   (-Z)
      4, 0, 3, 4, 3, 7, // Left   (-X)
      1, 5, 6, 1, 6, 2, // Right  (+X)
      3, 2, 6, 3, 6, 7, // Top    (+Y)
      4, 5, 1, 4, 1, 0, // Bottom (-Y)
  };

  // ---- Create the entity ----
  entity_ = std::make_unique<Cube>();
  auto &transform =
      entity_->get<ecs::component::object::TransformComponent<3>>();
  auto &mesh = entity_->get<ecs::component::object::Mesh>();
  auto &rc = entity_->get<ecs::component::object::RenderComponent>();

  // Build flat vertex data (GPUVertexPosition layout: 12 floats per vertex)
  constexpr uint32_t kFPV = ecs::component::object::Mesh::kFloatsPerVertex;
  mesh.dimension = 3;
  mesh.vertexData.resize(8 * kFPV, 0.0f);
  for (size_t i = 0; i < 8; ++i) {
    const size_t base = i * kFPV;
    mesh.vertexData[base + 0] = cubePos[i][0]; // x
    mesh.vertexData[base + 1] = cubePos[i][1]; // y
    mesh.vertexData[base + 2] = cubePos[i][2]; // z
    mesh.vertexData[base + 3] = static_cast<float>(mesh.dimension); // w = dimension tag
    mesh.vertexData[base + 4] = 1.0f;           // r
    mesh.vertexData[base + 5] = 1.0f;           // g
    mesh.vertexData[base + 6] = 1.0f;           // b
    // pad, normals default to zero/up (set by compute shader)
    mesh.vertexData[base + 9] = 1.0f;           // ny default
  }
  mesh.indices = std::move(indices);
  mesh.calculateFaces();

  if (!mesh.upload(allocator)) {
    return false;
  }

  // ---- Initialize the render component ----
  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eBack;
  pConfig.frontFace = vk::FrontFace::eCounterClockwise;
  pConfig.depthTestEnable = pConfig.depthWriteEnable = true;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages = vk::ShaderStageFlagBits::eVertex |
                               vk::ShaderStageFlagBits::eFragment |
                               vk::ShaderStageFlagBits::eCompute;

  if (!rc.initialize(allocator, device, mesh, *material_,
                     renderer.getRenderPass(), window::MAX_FRAMES_IN_FLIGHT,
                     pConfig, imageRegistry_->getDescriptorSetLayout()))
    return false;

  // Set the bindless texture ID and descriptor set
  rc.bindlessDescriptorSet = imageRegistry_->getDescriptorSet();
  rc.baseTextureId = cubeTextureId_;

  // Assign per-face materials using the FaceMaterial system
  // Each cube face = 2 triangles, 12 triangles total
  // Face 0 (front +Z, tri 0-1): texture from file, no effect
  {
    device::FaceMaterial fm;
    fm.textureId = cubeTextureId_.index;
    rc.setFaceMaterial(0, fm, mesh.getFaceCount());
    rc.setFaceMaterial(1, fm, mesh.getFaceCount());
  }
  // Face 1 (back -Z, tri 2-3): atlas layered texture, no effect
  {
    device::FaceMaterial fm;
    fm.textureId = atlasTextureId_.index;
    rc.setFaceMaterial(2, fm, mesh.getFaceCount());
    rc.setFaceMaterial(3, fm, mesh.getFaceCount());
  }
  // Face 2 (left -X, tri 4-5): gradient + wave
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.05f, 4.0f});
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
    rc.setFaceMaterial(4, fm, mesh.getFaceCount());
    rc.setFaceMaterial(5, fm, mesh.getFaceCount());
  }
  // Face 3 (right +X, tri 6-7): gradient + wave (same as left)
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.05f, 4.0f});
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
    rc.setFaceMaterial(6, fm, mesh.getFaceCount());
    rc.setFaceMaterial(7, fm, mesh.getFaceCount());
  }
  // Face 4 (top +Y, tri 8-9): plain colour, no effect
  rc.setFaceMaterial(8, {}, mesh.getFaceCount());
  rc.setFaceMaterial(9, {}, mesh.getFaceCount());
  // Face 5 (bottom -Y, tri 10-11): plain colour, no effect
  rc.setFaceMaterial(10, {}, mesh.getFaceCount());
  rc.setFaceMaterial(11, {}, mesh.getFaceCount());

  // Compute shader tags for wave displacement and normal precomputation
  static constexpr device::ShaderTag OBJECT_UPDATE_SHADER_TAG{
      "object_update", "object_update.slang", nullptr, nullptr,
      nullptr,         "updateMain"};
  static constexpr device::ShaderTag OBJECT_COMPUTE_SHADER_TAG{
      "object_compute", "object_compute.slang", nullptr, nullptr,
      nullptr,          "computeMain"};

  if (!rc.initializeCompute(device, shaderManager, &OBJECT_UPDATE_SHADER_TAG,
                            &OBJECT_COMPUTE_SHADER_TAG, mesh.vertexCount(),
                            static_cast<uint32_t>(mesh.indices.size())))
    return false;

  // Create shared frame data
  frameData_ = std::make_shared<window::Scene3DFrameData>();

  // Add object with update lambda that reads scene frame data
  addEntity(&rc, [this, &transform, &rc](uint32_t frameIndex) {
    float rotY = totalTime_ * 0.5f;
    float rotX = totalTime_ * 0.3f;
    transform.rotation = {rotX, rotY, 0.0f};
    rc.time = totalTime_;
    rc.updateUniforms(frameIndex, transform.modelMatrix(),
                      frameData_->view, frameData_->proj);
  });

  setLoaded(true);
  std::println("[{}] Cube scene loaded (bindless, textureId={}, "
               "atlasTextureId={})",
               getName(), cubeTextureId_.index, atlasTextureId_.index);
  return true;
}

void CubeScene3D::unload() {
  // Disconnect FrameUpdateSignal if still connected (e.g. when unloading
  // without going through Window::unpresentScene).
  if (frameUpdateSignal_ != nullptr && frameUpdateConnectionId_ != 0) {
    frameUpdateSignal_->disconnect(frameUpdateConnectionId_);
    frameUpdateSignal_       = nullptr;
    frameUpdateConnectionId_ = 0;
  }

  clearEntities();
  entity_.reset();
  if (material_) {
    material_->release();
  }
  material_.reset();
  textureTable_.reset();
  frameData_.reset();
  frameUpdateConnectionId_ = 0;
  setLoaded(false);
  std::println("[{}] Cube scene unloaded", getName());
}

void CubeScene3D::update(float deltaTime) {
  totalTime_ += deltaTime;

  if (!frameData_) {
    return;
  }

  // Update view/projection matrices (could be more sophisticated)
  const float aspect = 16.0f / 9.0f; // could be window aspect
  frameData_->view =
      glm::lookAt(glm::vec3(0.0f, 0.5f, 2.5f), glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  frameData_->proj =
      glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
  frameData_->proj[1][1] *= -1.0f;
}

void CubeScene3D::onComputeAttach(window::AsyncComputeManager *manager,
                                  window::FrameUpdateSignal *signal) {
  if (!manager || !entity_) {
    return;
  }

  auto &rc = entity_->get<ecs::component::object::RenderComponent>();

  // Register the displacement compute pass with Normal priority.
  // recordComputeCommands() is a no-op when no compute pipeline is present,
  // so this is safe to call unconditionally.
  manager->registerEffect(
      &rc, window::ComputePriority::Normal,
      [&rc](vk::CommandBuffer cmd, uint32_t frameIndex) {
        rc.recordComputeCommands(cmd, frameIndex);
      });

  // Connect to FrameUpdateSignal so rc.time is refreshed before compute runs.
  // Store the signal pointer and connection ID so onComputeDetach can
  // disconnect explicitly, preventing use-after-free if the scene outlives
  // the window's signal.
  if (signal) {
    auto result = signal->connect([this, &rc](float /*dt*/, uint32_t /*fi*/) {
      rc.time = totalTime_;
    });
    if (result.has_value()) {
      frameUpdateSignal_       = signal;
      frameUpdateConnectionId_ = result.value();
    }
  }
}

void CubeScene3D::onComputeDetach(window::AsyncComputeManager *manager) {
  // Disconnect from FrameUpdateSignal before the scene (and its captured
  // references) can be destroyed, preventing potential use-after-free.
  if (frameUpdateSignal_ != nullptr && frameUpdateConnectionId_ != 0) {
    frameUpdateSignal_->disconnect(frameUpdateConnectionId_);
    frameUpdateSignal_       = nullptr;
    frameUpdateConnectionId_ = 0;
  }

  if (!manager || !entity_) {
    return;
  }

  auto &rc = entity_->get<ecs::component::object::RenderComponent>();
  manager->unregisterEffect(&rc);
}
