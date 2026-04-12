#include "quad2d_scene.h"
#include "bindless_types.h"
#include "renderer.h"
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

  std::vector<window::Vertex<2>> vertices(9);
  for (size_t i = 0; i < 9; ++i) {
    vertices[i].position = {gridPositions[i].x, gridPositions[i].y};
    vertices[i].color = {1.0f, 1.0f, 1.0f};
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

  quad_ = std::make_unique<window::Object<2>>(
      QUAD_2D_OBJ_TAG, std::move(vertices), std::move(indices));

  // Set the bindless texture ID and descriptor set
  quad_->setTextureId(quadTextureId_);
  quad_->setBindlessDescriptorSet(imageRegistry_->getDescriptorSet());

  // Assign per-face materials (8 triangles = 4 quadrants x 2 tris)
  // Quadrant 0 (top-left, tri 0-1): plain colour, no effect
  quad_->setFaceMaterial(0, {});
  quad_->setFaceMaterial(1, {});
  // Quadrant 1 (top-right, tri 2-3): texture
  {
    device::FaceMaterial fm;
    fm.textureId = quadTextureId_.index;
    quad_->setFaceMaterial(2, fm);
    quad_->setFaceMaterial(3, fm);
  }
  // Quadrant 2 (bottom-left, tri 4-5): gradient effect
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
    quad_->setFaceMaterial(4, fm);
    quad_->setFaceMaterial(5, fm);
  }
  // Quadrant 3 (bottom-right, tri 6-7): wave effect
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.04f, 6.0f});
    quad_->setFaceMaterial(6, fm);
    quad_->setFaceMaterial(7, fm);
  }

  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eNone;
  pConfig.depthTestEnable = false;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages = vk::ShaderStageFlagBits::eVertex |
                               vk::ShaderStageFlagBits::eFragment |
                               vk::ShaderStageFlagBits::eCompute;

  if (!quad_->initialize(allocator, device, *material_,
                         renderer.getRenderPass(), window::MAX_FRAMES_IN_FLIGHT,
                         pConfig, imageRegistry_->getDescriptorSetLayout())) {
    std::println(stderr, "[{}] Failed to initialize 2D quad", getName());
    return false;
  }

  setLoaded(true);
  std::println("[{}] 2D scene loaded (bindless, textureId={})", getName(),
               quadTextureId_.index);
  return true;
}

void Quad2DScene::unload() {
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

void Quad2DScene::update(float deltaTime) { totalTime_ += deltaTime; }

void Quad2DScene::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
  if (quad_ && quad_->isInitialized()) {
    quad_->setTime(totalTime_);

    // 2D uses 3x3 matrices (identity for now)
    glm::mat3 view(1.0f);
    glm::mat3 proj(1.0f);
    quad_->updateUniforms(frameIndex, view, proj);
    quad_->draw(cmd, frameIndex);
  }
}
