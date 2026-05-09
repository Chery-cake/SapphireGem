#ifndef QUAD2D_SCENE_H_
#define QUAD2D_SCENE_H_

#include "entities.h"
#include "image_array_registry.h"
#include "object.h"
#include "scene.h"
#include "texture.h"
#include "texture_table.h"
#include <memory>

// Forward declarations for tags (defined in main.cpp)
extern const window::MaterialTag QUAD_2D_MATERIAL_TAG;

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
  std::shared_ptr<window::Scene2DFrameData> frameData_;
  float totalTime_ = 0.0f;

  // Shared texture (for CPU-side image loading/upload)
  std::shared_ptr<window::Texture> checkerboardTex_;

  // Shared bindless image registry (per-device, shared across scenes)
  std::shared_ptr<device::ImageArrayRegistry> imageRegistry_;
  std::shared_ptr<device::TextureTableManager> textureTable_;
  device::TextureId quadTextureId_;

  using Quad = ecs::entity::Tuple<ecs::component::object::TransformComponent<2>,
                                  ecs::component::object::Mesh<2>,
                                  ecs::component::object::RenderComponent<2>>;
  std::unique_ptr<Quad> entity_;

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
};

#endif // QUAD2D_SCENE_H_
