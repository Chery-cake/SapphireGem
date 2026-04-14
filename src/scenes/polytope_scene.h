#ifndef POLYTOPE_SCENE_H_
#define POLYTOPE_SCENE_H_

#include "image_array_registry.h"
#include "object.h"
#include "scene.h"
#include "texture.h"
#include "texture_table.h"
#include <memory>

// Forward declarations for tags (defined in main.cpp)
extern const window::MaterialTag POLYTOPE_MATERIAL_TAG;
extern const window::ObjectTag POLYTOPE_OBJ_TAG;

/**
 * @brief Random convex polytope demo scene with per-face materials
 *
 * Demonstrates:
 *  - Procedural geometry generation (random faces from icosahedron)
 *  - Per-face material variety (solid, gradient, texture, atlas, wave, drawing)
 *  - Multi-axis rotation animation
 */
class PolytopeDemoScene : public window::Scene {
private:
  std::unique_ptr<window::Material> material_;
  std::unique_ptr<window::Object<3>> polytope_;
  float rotX_ = 0.0f, rotY_ = 0.0f, rotZ_ = 0.0f;
  float totalTime_ = 0.0f;

  std::shared_ptr<window::Texture> checkerboardTex_;
  std::shared_ptr<window::Texture> layerAtlasTex_;
  std::shared_ptr<device::ImageArrayRegistry> imageRegistry_;
  std::shared_ptr<device::TextureTableManager> textureTable_;

public:
  explicit PolytopeDemoScene(
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

#endif // POLYTOPE_SCENE_H_
