#pragma once

#include "scene.h"

namespace scene {

// Scene 1: Basic shapes - triangles and cubes with colored vertices
class Scene1 : public render::Scene {
private:
  render::Object *triangle;
  render::Object *cube;

public:
  Scene1(render::MaterialManager *matMgr, render::TextureManager *texMgr,
         device::BufferManager *bufMgr, render::ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace scene
