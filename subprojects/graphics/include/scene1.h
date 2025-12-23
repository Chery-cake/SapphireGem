#pragma once

#include "scene.h"

namespace render {

// Scene 1: Basic shapes - triangles and cubes with colored vertices
class Scene1 : public Scene {
private:
  Object *triangle;
  Object *cube;

public:
  Scene1(MaterialManager *matMgr, TextureManager *texMgr,
         device::BufferManager *bufMgr, ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace render
