#pragma once

#include "scene.h"

namespace render {

// Scene 3: Multi-material objects - objects with multiple materials
class Scene3 : public Scene {
private:
  Object *multiMaterialQuad;
  Object *multiMaterialCube;

public:
  Scene3(MaterialManager *matMgr, TextureManager *texMgr,
         device::BufferManager *bufMgr, ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace render
