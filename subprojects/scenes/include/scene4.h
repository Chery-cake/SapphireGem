#pragma once

#include "scene.h"

namespace scene {

// Scene 4: Layered textures with animated shader background
class Scene4 : public render::Scene {
private:
  render::Object *quad; // Quad with 3-layer texture
  render::Object *cube; // Cube with incrementing layer count per face

public:
  Scene4(render::MaterialManager *matMgr, render::TextureManager *texMgr,
         device::BufferManager *bufMgr, render::ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace scene
