#include "quad2d_scene.h"
#include "bindless_types.h"
#include "object.h"
#include "renderer.h"
#include "scene.h"
#include "vulkan/vulkan.hpp"
#include <print>

Quad2DScene::Quad2DScene(
    const window::SceneTag &sceneTag,
    std::shared_ptr<window::Texture> checkerboard,
    std::shared_ptr<device::ImageArrayRegistry> registry,
    std::shared_ptr<device::TextureTableManager> textureTable)
    : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
      imageRegistry_(std::move(registry)),
      textureTable_(std::move(textureTable)) {}

bool Quad2DScene::load(device::GPUDevice &device,
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
      checkerHandle = imageRegistry_->registerImage(device::ImageKind::eImage2D,
                                                    layer->gpuImage.getView());
    }
  }

  // Build a texture record with one layer (checkerboard image)
  quadTextureId_ = textureTable_->addRecord(1);

  device::GPUTextureLayer quadLayer;
  quadLayer.image2DIndex =
      checkerHandle.isValid() ? static_cast<int32_t>(checkerHandle.index) : -1;
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

  // 2D quad: 9 unique vertices + 24 indices (4 quadrants × 2 triangles × 3
  // verts) Using indexed drawing so the center vertex (shared by all 4
  // quadrants) correctly receives wave displacement from any quadrant's
  // effects.
  //
  // Vertex layout (3×3 grid):
  //   6---7---8
  //   |  Q2 | Q3 |
  //   3---4---5
  //   |  Q0 | Q1 |
  //   0---1---2
  //
  // Quadrant layout (Vulkan NDC, Y-down):
  //   Face 0 (top-left):     Q0 = triangles 0-1 (verts 3,4,7 / 3,7,6)
  //   Face 1 (top-right):    Q1 = triangles 2-3 (verts 4,5,8 / 4,8,7)
  //   Face 2 (bottom-left):  Q2 = triangles 4-5 (verts 0,1,4 / 0,4,3)
  //   Face 3 (bottom-right): Q3 = triangles 6-7 (verts 1,2,5 / 1,5,4)
  struct P2 {
    float x, y;
  };
  static constexpr std::array<P2, 9> gridPositions = {
      P2(-0.8f, -0.8f), // 0: bottom-left
      P2(0.0f, -0.8f),  // 1: bottom-center
      P2(0.8f, -0.8f),  // 2: bottom-right
      P2(-0.8f, 0.0f),  // 3: middle-left
      P2(0.0f, 0.0f),   // 4: center (shared by all quadrants)
      P2(0.8f, 0.0f),   // 5: middle-right
      P2(-0.8f, 0.8f),  // 6: top-left
      P2(0.0f, 0.8f),   // 7: top-center
      P2(0.8f, 0.8f),   // 8: top-right
  };

  entity_ = std::make_unique<Quad>();
  auto &transform =
      entity_->get<ecs::component::object::TransformComponent<2>>();
  auto &mesh = entity_->get<ecs::component::object::Mesh>();
  auto &rc = entity_->get<ecs::component::object::RenderComponent>();

  // Build flat vertex data (GPUVertexPosition layout: 12 floats per vertex)
  constexpr uint32_t kFPV = ecs::component::object::Mesh::kFloatsPerVertex;
  mesh.dimension = 2;
  mesh.vertexData.resize(9 * kFPV, 0.0f);
  for (size_t i = 0; i < 9; ++i) {
    const size_t base = i * kFPV;
    mesh.vertexData[base + 0] = gridPositions[i].x; // x
    mesh.vertexData[base + 1] = gridPositions[i].y; // y
    // z stays 0.0f
    mesh.vertexData[base + 3] = static_cast<float>(mesh.dimension); // w = dimension tag
    mesh.vertexData[base + 4] = 1.0f;               // r
    mesh.vertexData[base + 5] = 1.0f;               // g
    mesh.vertexData[base + 6] = 1.0f;               // b
    mesh.vertexData[base + 9] = 1.0f;               // ny default
  }

  std::vector<uint32_t> indices = {
      // Face 0 (top-left quadrant, tri 0-1)
      3,
      4,
      7,
      3,
      7,
      6,
      // Face 1 (top-right quadrant, tri 2-3)
      4,
      5,
      8,
      4,
      8,
      7,
      // Face 2 (bottom-left quadrant, tri 4-5)
      0,
      1,
      4,
      0,
      4,
      3,
      // Face 3 (bottom-right quadrant, tri 6-7)
      1,
      2,
      5,
      1,
      5,
      4,
  };

  mesh.indices = std::move(indices);
  mesh.calculateFaces();

  if (!mesh.upload(allocator)) {
    return false;
  }

  // ---- Initialize the render component ----
  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eNone;
  pConfig.frontFace = vk::FrontFace::eCounterClockwise;
  pConfig.depthTestEnable = pConfig.depthWriteEnable = false;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages = vk::ShaderStageFlagBits::eVertex |
                               vk::ShaderStageFlagBits::eFragment |
                               vk::ShaderStageFlagBits::eCompute;

  if (!rc.initialize(allocator, device, mesh, *material_,
                     renderer.getRenderPass(), window::MAX_FRAMES_IN_FLIGHT,
                     pConfig, imageRegistry_->getDescriptorSetLayout()))
    return false;

  // Set the bindless texture ID and descriptor set
  rc.baseTextureId = quadTextureId_;
  rc.bindlessDescriptorSet = imageRegistry_->getDescriptorSet();

  // Assign per-face materials (8 triangles = 4 quadrants x 2 tris)
  // Quadrant 0 (top-left, tri 0-1): plain colour, no effect
  rc.setFaceMaterial(0, {}, mesh.getFaceCount());
  rc.setFaceMaterial(1, {}, mesh.getFaceCount());
  // Quadrant 1 (top-right, tri 2-3): texture
  {
    device::FaceMaterial fm;
    fm.textureId = quadTextureId_.index;
    rc.setFaceMaterial(2, fm, mesh.getFaceCount());
    rc.setFaceMaterial(3, fm, mesh.getFaceCount());
  }
  // Quadrant 2 (bottom-left, tri 4-5): gradient effect
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
    rc.setFaceMaterial(4, fm, mesh.getFaceCount());
    rc.setFaceMaterial(5, fm, mesh.getFaceCount());
  }
  // Quadrant 3 (bottom-right, tri 6-7): wave effect
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.04f, 6.0f});
    rc.setFaceMaterial(6, fm, mesh.getFaceCount());
    rc.setFaceMaterial(7, fm, mesh.getFaceCount());
  }

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

  frameData_ = std::make_shared<window::Scene2DFrameData>();
  addEntity(&rc, [this, &transform, &rc](uint32_t frameIndex) {
    float rotY = totalTime_ * 0.5f;
    float rotX = totalTime_ * 0.3f;
    transform.rotation = {rotX, rotY};
    rc.time = totalTime_;
    rc.updateUniforms(frameIndex, transform.modelMatrix(),
                      frameData_->view, frameData_->proj);
  });

  setLoaded(true);
  std::println("[{}] 2D scene loaded (bindless, textureId={})", getName(),
               quadTextureId_.index);
  return true;
}

void Quad2DScene::unload() {
  clearEntities();
  if (material_) {
    material_->release();
  }
  material_.reset();
  textureTable_.reset();
  setLoaded(false);
  std::println("[{}] 2D scene unloaded", getName());
}

void Quad2DScene::update(float deltaTime) { totalTime_ += deltaTime; }
