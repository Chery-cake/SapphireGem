#include "polytope_scene.h"
#include "bindless_types.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "renderer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <print>
#include <random>
#include <ranges>

PolytopeDemoScene::PolytopeDemoScene(
    const window::SceneTag &sceneTag,
    std::shared_ptr<window::Texture> checkerboard,
    std::shared_ptr<window::Texture> layerAtlas,
    std::shared_ptr<device::ImageArrayRegistry> registry,
    std::shared_ptr<device::TextureTableManager> textureTable)
    : Scene(sceneTag), checkerboardTex_(std::move(checkerboard)),
      layerAtlasTex_(std::move(layerAtlas)),
      imageRegistry_(std::move(registry)),
      textureTable_(std::move(textureTable)) {}

bool PolytopeDemoScene::load(device::GPUDevice &device,
                             std::vector<device::GPUDevice *> &secondaryGPUs,
                             device::VMAAllocator &allocator,
                             device::ShaderManager &shaderManager,
                             window::Renderer &renderer) {
  (void)secondaryGPUs;

  if (!imageRegistry_->isInitialized()) {
    if (!imageRegistry_->initialize(device)) {
      std::println(stderr, "[{}] Failed to initialize image registry",
                   getName());
      return false;
    }
  }

  material_ = std::make_unique<window::Material>(POLYTOPE_MATERIAL_TAG);
  if (!material_->initialize(shaderManager)) {
    std::println(stderr, "[{}] Failed to initialize polytope material",
                 getName());
    return false;
  }

  // Upload textures if not already done
  if (checkerboardTex_ && !checkerboardTex_->isUploaded()) {
    if (!checkerboardTex_->upload(allocator, device)) {
      std::println(stderr, "[{}] Failed to upload checkerboard texture",
                   getName());
      return false;
    }
    checkerboardTex_->createSampler(device);
  }
  if (layerAtlasTex_ && !layerAtlasTex_->isUploaded()) {
    if (!layerAtlasTex_->upload(allocator, device)) {
      std::println(stderr, "[{}] Failed to upload layer atlas texture",
                   getName());
      return false;
    }
    layerAtlasTex_->createSampler(device);
  }

  // Register images in the shared bindless registry
  device::ImageHandle checkerHandle;
  if (checkerboardTex_ && checkerboardTex_->isUploaded()) {
    const auto *layer = checkerboardTex_->getLayer(0);
    if (layer && layer->loaded) {
      checkerHandle = imageRegistry_->registerImage(device::ImageKind::eImage2D,
                                                    layer->gpuImage.getView());
    }
  }

  device::ImageHandle atlasHandle;
  if (layerAtlasTex_ && layerAtlasTex_->isUploaded()) {
    const auto *layer = layerAtlasTex_->getLayer(0);
    if (layer && layer->loaded) {
      atlasHandle = imageRegistry_->registerImage(device::ImageKind::eAtlas,
                                                  layer->gpuImage.getView());
    }
  }

  // Generate a random convex polytope
  // Fixed seed (42) for reproducible polytope generation across runs.
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> faceDist(6, 12);
  int targetFaces = faceDist(rng);

  // Golden ratio icosahedron vertices
  const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
  const float scale = 0.5f / std::sqrt(1.0f + phi * phi);

  std::vector<glm::vec3> icoVerts = {
      {-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
      {0, -1, phi}, {0, 1, phi}, {0, -1, -phi}, {0, 1, -phi},
      {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1},
  };
  for (auto &v : icoVerts) {
    v *= scale;
  }

  // Icosahedron face indices (20 triangles)
  std::vector<std::array<int, 3>> icoFaces = {
      {0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
      {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
      {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
      {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1},
  };

  // Select up to targetFaces from the icosahedron
  int numFaces = std::min(targetFaces, static_cast<int>(icoFaces.size()));

  // Shuffle faces and pick the first N
  std::vector<int> faceIndices(static_cast<size_t>(icoFaces.size()));
  std::iota(faceIndices.begin(), faceIndices.end(), 0);
  std::ranges::shuffle(faceIndices, rng);

  // Random colours
  std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);

  std::vector<window::Vertex<3>> vertices;
  std::vector<uint32_t> indices;

  for (int fi = 0; fi < numFaces; ++fi) {
    auto &face =
        icoFaces[static_cast<size_t>(faceIndices[static_cast<size_t>(fi)])];
    float r = colorDist(rng), g = colorDist(rng), b = colorDist(rng);

    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
    for (int vi = 0; vi < 3; ++vi) {
      window::Vertex<3> vert;
      auto &p = icoVerts[static_cast<size_t>(face[static_cast<size_t>(vi)])];
      vert.position = {p.x, p.y, p.z};
      vert.color = {r, g, b};
      vertices.push_back(vert);
    }
    indices.push_back(baseIdx);
    indices.push_back(baseIdx + 1);
    indices.push_back(baseIdx + 2);
  }

  polytope_ = std::make_unique<window::Object<3>>(
      POLYTOPE_OBJ_TAG, std::move(vertices), std::move(indices));

  // Assign random per-face materials
  std::uniform_int_distribution<int> typeDist(0, 5);

  for (int fi = 0; fi < numFaces; ++fi) {
    uint32_t faceIdx = static_cast<uint32_t>(fi);
    int matType = typeDist(rng);
    device::FaceMaterial fm;

    switch (matType) {
    case 0: // Solid colour — no texture, no effect
      break;
    case 1: // Gradient
      (void)fm.addEffect(
          device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
      break;
    case 2: { // Single image file (checkerboard)
      device::TextureId texId = textureTable_->addRecord(1);
      device::GPUTextureLayer layer;
      layer.image2DIndex = checkerHandle.isValid()
                               ? static_cast<int32_t>(checkerHandle.index)
                               : -1;
      textureTable_->setLayers(texId, {layer});
      fm.textureId = texId.index;
      break;
    }
    case 3: { // Image from atlas (random sub-region)
      device::TextureId texId = textureTable_->addRecord(1);
      device::GPUTextureLayer layer;
      layer.atlasIndex =
          atlasHandle.isValid() ? static_cast<int32_t>(atlasHandle.index) : -1;
      std::uniform_real_distribution<float> uvDist(0.0f, 0.5f);
      layer.atlasUvOffsetX = uvDist(rng);
      layer.atlasUvOffsetY = uvDist(rng);
      layer.atlasUvScaleX = 0.5f;
      layer.atlasUvScaleY = 1.0f / 3.0f;
      textureTable_->setLayers(texId, {layer});
      fm.textureId = texId.index;
      break;
    }
    case 4: // Wave effect
      (void)fm.addEffect(
          device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
      break;
    case 5: // Drawing effect
      (void)fm.addEffect(device::FaceEffect{device::EffectType::eDrawing});
      break;
    }

    polytope_->setFaceMaterial(faceIdx, fm);
  }

  // Upload texture tables to GPU
  if (!textureTable_->uploadToGPU(allocator, device)) {
    std::println(stderr, "[{}] Failed to upload texture tables", getName());
    return false;
  }

  if (checkerboardTex_ && checkerboardTex_->getSampler()) {
    imageRegistry_->commitDescriptors(device, checkerboardTex_->getSampler(),
                                      &textureTable_->getRecordBuffer(),
                                      &textureTable_->getLayerBuffer());
  }

  polytope_->setBindlessDescriptorSet(imageRegistry_->getDescriptorSet());

  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eNone;
  pConfig.frontFace = vk::FrontFace::eCounterClockwise;
  pConfig.depthTestEnable = true;
  pConfig.depthWriteEnable = true;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;

  if (!polytope_->initialize(allocator, device, *material_,
                             renderer.getRenderPass(),
                             window::MAX_FRAMES_IN_FLIGHT, pConfig,
                             imageRegistry_->getDescriptorSetLayout())) {
    std::println(stderr, "[{}] Failed to initialize polytope", getName());
    return false;
  }

  setLoaded(true);
  std::println("[{}] Polytope scene loaded ({} faces)", getName(), numFaces);
  return true;
}

void PolytopeDemoScene::unload() {
  if (polytope_) {
    polytope_->release();
    polytope_.reset();
  }
  if (material_) {
    material_->release();
    material_.reset();
  }
  setLoaded(false);
  std::println("[{}] Polytope scene unloaded", getName());
}

void PolytopeDemoScene::update(float deltaTime) {
  totalTime_ += deltaTime;
  rotX_ += deltaTime * 0.7f;
  rotY_ += deltaTime * 1.1f;
  rotZ_ += deltaTime * 0.4f;
}

void PolytopeDemoScene::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
  if (polytope_ && polytope_->isInitialized()) {
    polytope_->setRotation({rotX_, rotY_, rotZ_});
    polytope_->setTime(totalTime_);

    glm::mat4 view =
        glm::lookAt(glm::vec3(0.0f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                    glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj =
        glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f; // Flip Y for Vulkan

    polytope_->updateUniforms(frameIndex, view, proj);
    polytope_->draw(cmd, frameIndex);
  }
}
