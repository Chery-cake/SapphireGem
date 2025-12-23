#pragma once

#include "scene.h"

namespace scene {

// Scene 2: Textured objects - various textured quads and shapes
class Scene2 : public render::Scene {
private:
  render::Object *texturedSquare;
  render::Object *imageQuad;
  render::Object *atlasQuad1;
  render::Object *atlasQuad2;
  render::Object *atlasQuad3;
  render::Object *atlasQuad4;

public:
  Scene2(render::MaterialManager *matMgr, render::TextureManager *texMgr,
         device::BufferManager *bufMgr, render::ObjectManager *objMgr);

  void setup() override;
  void update(float deltaTime, float totalTime) override;
  std::string get_name() const override;
};

} // namespace scene
