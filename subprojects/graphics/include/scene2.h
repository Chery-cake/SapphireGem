#pragma once

#include "scene.h"

namespace render {

// Scene 2: Textured objects - various textured quads and shapes
class Scene2 : public Scene {
private:
  Object *texturedSquare;
  Object *imageQuad;
  Object *atlasQuad1;
  Object *atlasQuad2;
  Object *atlasQuad3;
  Object *atlasQuad4;

public:
  Scene2(MaterialManager *matMgr, TextureManager *texMgr,
         device::BufferManager *bufMgr, ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace render
