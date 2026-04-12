#ifndef QUAD2D_SCENE_H_
#define QUAD2D_SCENE_H_

#include "image_array_registry.h"
#include "object.h"
#include "scene.h"
#include "texture.h"
#include "texture_table.h"
#include <memory>

// Forward declarations for tags (defined in main.cpp)
extern const window::MaterialTag QUAD_2D_MATERIAL_TAG;
extern const window::ObjectTag QUAD_2D_OBJ_TAG;

/**
 * @brief 2D quad scene with bindless textures
 *
 * Demonstrates:
 *  - 2D object rendering with mat3 UBO
 *  - Bindless texture sampling
 */
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
      std::shared_ptr<device::TextureTableManager> textureTable);

  bool load(device::GPUDevice &device,
            std::vector<device::GPUDevice *> &secondaryGPUs,
            device::VMAAllocator &allocator,
            device::ShaderManager &shaderManager,
            window::Renderer &renderer) override;

  void unload() override;
  void update(float deltaTime) override;
  void preRender(vk::CommandBuffer cmd, uint32_t frameIndex) override;
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) override;
};

#endif // QUAD2D_SCENE_H_
