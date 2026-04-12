#include "quad2d_scene.h"
#include "bindless_types.h"
#include "renderer.h"
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

  // 2D quad: 9 shared vertices (3×3 grid) with indexed drawing.
  // The 4 quadrants share boundary edges along x=0 and y=0. Using shared
  // vertices ensures boundary positions are transformed identically for
  // all adjacent faces, preventing seams between wave and non-wave quadrants.
  //
  // Vertex layout (3×3 grid):
  //   6---7---8    y=0.8
  //   |  0|  1|
  //   3---4---5    y=0.0
  //   |  2|  3|
  //   0---1---2    y=-0.8
  //       x=-0.8  0.0  0.8
  //
  // Quadrant layout (Vulkan NDC, Y-down):
  //   Quadrant 0 (top-left):     triangles 0-1
  //   Quadrant 1 (top-right):    triangles 2-3
  //   Quadrant 2 (bottom-left):  triangles 4-5
  //   Quadrant 3 (bottom-right): triangles 6-7

  // 9 unique vertex positions
  static constexpr float xs[3] = {-0.8f, 0.0f, 0.8f};
  static constexpr float ys[3] = {-0.8f, 0.0f, 0.8f};

  std::vector<window::Vertex<2>> vertices(9);
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      size_t idx = static_cast<size_t>(row * 3 + col);
      vertices[idx].position = {xs[col], ys[row]};
      vertices[idx].color = {1.0f, 1.0f, 1.0f};
    }
  }

  // 24-element index buffer (8 triangles = 4 quadrants × 2 tris)
  // Vertex indices reference the 3×3 grid:
  //   0=(−0.8,−0.8) 1=(0,−0.8) 2=(0.8,−0.8)
  //   3=(−0.8,0)    4=(0,0)    5=(0.8,0)
  //   6=(−0.8,0.8)  7=(0,0.8)  8=(0.8,0.8)
  std::vector<uint32_t> indices = {
      // Quadrant 0 (top-left): vertices 3,4,7,6
      3, 4, 7,   // tri 0
      3, 7, 6,   // tri 1
      // Quadrant 1 (top-right): vertices 4,5,8,7
      4, 5, 8,   // tri 2
      4, 8, 7,   // tri 3
      // Quadrant 2 (bottom-left): vertices 0,1,4,3
      0, 1, 4,   // tri 4
      0, 4, 3,   // tri 5
      // Quadrant 3 (bottom-right): vertices 1,2,5,4
      1, 2, 5,   // tri 6
      1, 5, 4,   // tri 7
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
  pConfig.pushConstantStages =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
      vk::ShaderStageFlagBits::eFragment;

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
