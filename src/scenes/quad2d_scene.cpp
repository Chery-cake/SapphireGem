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

  // 2D quad: 24 vertices (4 faces x 2 triangles x 3 vertices)
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
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

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
