#ifndef CUBE_SCENE_H_
#define CUBE_SCENE_H_

#include "async_compute_manager.h"
#include "entities.h"
#include "frame_update_signal.h"
#include "image_array_registry.h"
#include "object.h"
#include "scene.h"
#include "texture.h"
#include "texture_table.h"
#include <cstddef>
#include <memory>

// Forward declarations for tags (defined in main.cpp)
extern const window::MaterialTag CUBE_MATERIAL_TAG;

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
  std::shared_ptr<window::Scene3DFrameData> frameData_;
  float totalTime_ = 0.0f;

  // Shared textures (for CPU-side image loading/upload)
  std::shared_ptr<window::Texture> checkerboardTex_;
  std::shared_ptr<window::Texture> layerAtlasTex_;

  // Shared bindless image registry (per-device, shared across scenes)
  std::shared_ptr<device::ImageArrayRegistry> imageRegistry_;
  std::shared_ptr<device::TextureTableManager> textureTable_;
  device::TextureId cubeTextureId_;
  device::TextureId atlasTextureId_;

  using Cube = ecs::entity::Tuple<ecs::component::object::TransformComponent<3>,
                                   ecs::component::object::Mesh,
                                   ecs::component::object::RenderComponent>;
  std::unique_ptr<Cube> entity_;

  // Signal connection ID for FrameUpdateSignal subscription
  window::FrameUpdateSignal *frameUpdateSignal_ = nullptr;
  uint64_t frameUpdateConnectionId_ = 0;

public:
  CubeScene3D(const window::SceneTag &sceneTag,
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

  /**
   * @brief Register the cube's compute displacement effect.
   *
   * Connects to @p signal so that @c rc.time is updated each frame before
   * the compute pass runs, then registers
   * @ref ecs::component::object::RenderComponent::recordComputeCommands with
   * @p manager at Normal priority.
   */
  void onComputeAttach(window::AsyncComputeManager *manager,
                       window::FrameUpdateSignal *signal) override;

  /**
   * @brief Unregister the cube's compute effect.
   */
  void onComputeDetach(window::AsyncComputeManager *manager) override;
};

#endif // CUBE_SCENE_H_
