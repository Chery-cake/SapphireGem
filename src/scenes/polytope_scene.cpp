#include "polytope_scene.h"
#include "bindless_types.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include "renderer.h"
#include "vulkan/vulkan.hpp"
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

  // Generate icosahedron: 12 unique vertices + 60 indices (20 triangles)
  // Using indexed drawing so shared vertices get consistent displacement.

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

  int numFaces = static_cast<int>(icoFaces.size());

  // Random colours per face (fixed seed for reproducibility)
  std::mt19937 rng(42);
  std::uniform_real_distribution<float> colorDist(0.2f, 1.0f);

  // Build 12 unique vertices with average colour from adjacent faces
  std::vector<ecs::component::object::Vertex<3>> vertices(12);
  // Track per-vertex colour accumulation
  std::vector<glm::vec3> colorAccum(12, glm::vec3(0.0f));
  std::vector<int> colorCount(12, 0);

  // First pass: determine per-face colours and accumulate onto vertices
  std::vector<glm::vec3> faceColors(static_cast<size_t>(numFaces));
  for (int fi = 0; fi < numFaces; ++fi) {
    faceColors[static_cast<size_t>(fi)] = {colorDist(rng), colorDist(rng),
                                           colorDist(rng)};
    for (int k = 0; k < 3; ++k) {
      int vi = icoFaces[static_cast<size_t>(fi)][static_cast<size_t>(k)];
      colorAccum[static_cast<size_t>(vi)] +=
          faceColors[static_cast<size_t>(fi)];
      colorCount[static_cast<size_t>(vi)]++;
    }
  }

  // Set vertex positions and averaged colours
  for (size_t i = 0; i < 12; ++i) {
    vertices[i].position = {icoVerts[i].x, icoVerts[i].y, icoVerts[i].z};
    glm::vec3 avgColor = colorCount[i] > 0
                             ? colorAccum[i] / static_cast<float>(colorCount[i])
                             : glm::vec3(1.0f);
    vertices[i].color = {avgColor.r, avgColor.g, avgColor.b};
  }

  // Build index buffer with correct outward-facing winding
  std::vector<uint32_t> indices;
  indices.reserve(60);

  for (int fi = 0; fi < numFaces; ++fi) {
    auto &face = icoFaces[static_cast<size_t>(fi)];
    int i0 = face[0], i1 = face[1], i2 = face[2];

    // Ensure outward-facing winding (CCW when viewed from outside).
    glm::vec3 p0 = icoVerts[static_cast<size_t>(i0)];
    glm::vec3 p1 = icoVerts[static_cast<size_t>(i1)];
    glm::vec3 p2 = icoVerts[static_cast<size_t>(i2)];
    glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
    glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
    if (glm::dot(normal, centroid) < 0.0f) {
      std::swap(i1, i2);
    }

    indices.push_back(static_cast<uint32_t>(i0));
    indices.push_back(static_cast<uint32_t>(i1));
    indices.push_back(static_cast<uint32_t>(i2));
  }

  entity_ = std::make_unique<Poly>();
  auto &transform =
      entity_->get<ecs::component::object::TransformComponent<3>>();
  auto &mesh = entity_->get<ecs::component::object::Mesh<3>>();
  auto &rc = entity_->get<ecs::component::object::RenderComponent<3>>();

  mesh.vertices = std::move(vertices);
  mesh.indices = std::move(indices);
  mesh.calculateFaces();

  if (!mesh.upload(allocator)) {
    return false;
  }

  rc.bindlessDescriptorSet = imageRegistry_->getDescriptorSet();

  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eBack;
  pConfig.frontFace = vk::FrontFace::eCounterClockwise;
  pConfig.depthTestEnable = true;
  pConfig.depthWriteEnable = true;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages = vk::ShaderStageFlagBits::eVertex |
                               vk::ShaderStageFlagBits::eFragment |
                               vk::ShaderStageFlagBits::eCompute;

  if (!rc.initialize(allocator, device, mesh, *material_,
                     renderer.getRenderPass(), window::MAX_FRAMES_IN_FLIGHT,
                     pConfig, imageRegistry_->getDescriptorSetLayout()))
    return false;

  // Assign per-face materials cycling through material types
  for (int fi = 0; fi < numFaces; ++fi) {
    uint32_t faceIdx = static_cast<uint32_t>(fi);
    int matType = fi % 6; // Cycle through all material types deterministically
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

    rc.setFaceMaterial(faceIdx, fm, mesh.getFaceCount());
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

  // Compute shader tags for wave displacement and normal precomputation
  static constexpr device::ShaderTag OBJECT_UPDATE_SHADER_TAG{
      "object_update", "object_update.slang", nullptr, nullptr,
      nullptr,         "updateMain"};
  static constexpr device::ShaderTag OBJECT_COMPUTE_SHADER_TAG{
      "object_compute", "object_compute.slang", nullptr, nullptr,
      nullptr,          "computeMain"};

  if (!rc.initializeCompute(device, shaderManager, &OBJECT_UPDATE_SHADER_TAG,
                            &OBJECT_COMPUTE_SHADER_TAG, mesh.vertices.size(),
                            mesh.indices.size()))
    return false;

  frameData_ = std::make_shared<window::Scene3DFrameData>();
  addEntity(&rc, [this, &transform, &rc](uint32_t frameIndex) {
    transform.rotation = {rotX_, rotY_, rotZ_};
    rc.time = totalTime_;
    rc.updateUniforms(frameIndex, transform, frameData_->view,
                      frameData_->proj);
  });

  setLoaded(true);
  std::println("[{}] Polytope scene loaded ({} faces)", getName(), numFaces);
  return true;
}

void PolytopeDemoScene::unload() {
  clearEntities();
  entity_.reset();
  if (material_) {
    material_->release();
  }
  material_.reset();
  setLoaded(false);
  std::println("[{}] Polytope scene unloaded", getName());
}

void PolytopeDemoScene::update(float deltaTime) {
  totalTime_ += deltaTime;
  rotX_ += deltaTime * 0.7f;
  rotY_ += deltaTime * 1.1f;
  rotZ_ += deltaTime * 0.4f;

  if (!frameData_) {
    return;
  }

  // compute view/proj
  frameData_->view =
      glm::lookAt(glm::vec3(0.0f, 0.5f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, 1.0f, 0.0f));
  float aspect = 16.0f / 9.0f;
  frameData_->proj =
      glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
  frameData_->proj[1][1] *= -1.0f;
}
