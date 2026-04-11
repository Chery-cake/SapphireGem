#pragma once

#include "image_array_registry.h"
#include "object.h"
#include "scene.h"
#include "texture.h"
#include "texture_table.h"
#include <memory>

// Forward declarations for tags (defined in main.cpp)
extern const window::MaterialTag CUBE_MATERIAL_TAG;
extern const window::ObjectTag CUBE_OBJ_TAG;

/**
 * @brief 3D cube scene with per-face materials and bindless textures
 *
 * Demonstrates:
 *  - Bindless texture sampling (checkerboard + layered atlas)
 *  - Per-face effects (gradient, wave) via the FaceMaterial system
 *  - Geometry shader lighting
 */
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
      std::shared_ptr<device::TextureTableManager> textureTable);

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override;

  void unload() override;
  void update(float deltaTime) override;
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) override;
};
