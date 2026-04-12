#include "cube_scene.h"
#include "bindless_types.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "renderer.h"
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

  // Create cube geometry with 8 shared vertices and indexed drawing.
  // The 8 corner positions are stored once as unique vertices, and the
  // 36-element index buffer references into them (12 triangles, 6 faces).
  // This ensures shared corner vertices are transformed identically for
  // all faces, eliminating seams between wave-effect and non-wave faces.
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

  // 8 unique vertices (the cube corners)
  std::vector<window::Vertex<3>> vertices(8);
  for (size_t i = 0; i < 8; ++i) {
    vertices[i].position = {cubePos[i][0], cubePos[i][1], cubePos[i][2]};
    vertices[i].color = {1.0f, 1.0f, 1.0f};
  }

  // 36-element index buffer referencing the 8 shared vertices
  std::vector<uint32_t> indices = {
      0, 1, 2, 0, 2, 3, // Front  (+Z)
      5, 4, 7, 5, 7, 6, // Back   (-Z)
      4, 0, 3, 4, 3, 7, // Left   (-X)
      1, 5, 6, 1, 6, 2, // Right  (+X)
      3, 2, 6, 3, 6, 7, // Top    (+Y)
      4, 5, 1, 4, 1, 0, // Bottom (-Y)
  };

  cube_ = std::make_unique<window::Object<3>>(CUBE_OBJ_TAG, std::move(vertices),
                                              std::move(indices));

  // Set the bindless texture ID and descriptor set
  cube_->setTextureId(cubeTextureId_);
  cube_->setBindlessDescriptorSet(imageRegistry_->getDescriptorSet());

  // Assign per-face materials using the FaceMaterial system
  // Each cube face = 2 triangles, 12 triangles total
  // Face 0 (front +Z, tri 0-1): texture from file, no effect
  {
    device::FaceMaterial fm;
    fm.textureId = cubeTextureId_.index;
    cube_->setFaceMaterial(0, fm);
    cube_->setFaceMaterial(1, fm);
  }
  // Face 1 (back -Z, tri 2-3): atlas layered texture, no effect
  {
    device::FaceMaterial fm;
    fm.textureId = atlasTextureId_.index;
    cube_->setFaceMaterial(2, fm);
    cube_->setFaceMaterial(3, fm);
  }
  // Face 2 (left -X, tri 4-5): gradient + wave
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.05f, 4.0f});
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
    cube_->setFaceMaterial(4, fm);
    cube_->setFaceMaterial(5, fm);
  }
  // Face 3 (right +X, tri 6-7): gradient + wave (same as left)
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.05f, 4.0f});
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
    cube_->setFaceMaterial(6, fm);
    cube_->setFaceMaterial(7, fm);
  }
  // Face 4 (top +Y, tri 8-9): plain colour, no effect
  cube_->setFaceMaterial(8, {});
  cube_->setFaceMaterial(9, {});
  // Face 5 (bottom -Y, tri 10-11): plain colour, no effect
  cube_->setFaceMaterial(10, {});
  cube_->setFaceMaterial(11, {});

  // Pipeline config with push constants for time + objectId
  window::PipelineConfig pConfig;
  pConfig.topology = vk::PrimitiveTopology::eTriangleList;
  pConfig.cullMode = vk::CullModeFlagBits::eBack;
  pConfig.frontFace = vk::FrontFace::eCounterClockwise;
  pConfig.depthTestEnable = true;
  pConfig.depthWriteEnable = true;
  pConfig.pushConstantSize = sizeof(device::BindlessPushConstants);
  pConfig.pushConstantStages =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
      vk::ShaderStageFlagBits::eFragment;

  if (!cube_->initialize(allocator, device, *material_,
                         renderer.getRenderPass(), window::MAX_FRAMES_IN_FLIGHT,
                         pConfig, imageRegistry_->getDescriptorSetLayout())) {
    std::println(stderr, "[{}] Failed to initialize cube", getName());
    return false;
  }

  setLoaded(true);
  std::println("[{}] Cube scene loaded (bindless, textureId={}, "
               "atlasTextureId={})",
               getName(), cubeTextureId_.index, atlasTextureId_.index);
  return true;
}

void CubeScene3D::unload() {
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

void CubeScene3D::update(float deltaTime) { totalTime_ += deltaTime; }

void CubeScene3D::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
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
