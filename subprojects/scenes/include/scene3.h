#pragma once

#include "scene.h"

namespace scene {

// Scene 3: Multi-material objects - objects with multiple materials
class Scene3 : public render::Scene {
private:
  render::Object *multiMaterialQuad;
  render::Object *multiMaterialCube;

public:
  Scene3(render::MaterialManager *matMgr, render::TextureManager *texMgr,
         device::BufferManager *bufMgr, render::ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace scene
