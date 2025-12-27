#pragma once

#include "scene.h"

namespace scene {

// Scene 5: 3D object with 6 faces, each using different shaders and textures
// Demonstrates advanced shader composition with geometry shaders
class Scene5 : public render::Scene {
private:
  render::Object *cube; // Cube with different shaders per face

public:
  Scene5(render::MaterialManager *matMgr, render::TextureManager *texMgr,
         device::BufferManager *bufMgr, render::ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace scene
