#include "scene1.h"
#include <print>

namespace scene {

Scene1::Scene1(render::MaterialManager *matMgr, render::TextureManager *texMgr,
               device::BufferManager *bufMgr, render::ObjectManager *objMgr)
    : render::Scene(matMgr, texMgr, bufMgr, objMgr), triangle(nullptr), cube(nullptr) {
}

void Scene1::setup() {
  std::print("Setting up Scene 1: Basic Shapes\n");

  // Create basic materials
  create_basic_material(render::MaterialId::SIMPLE_SHADERS_2D, true, false);
  create_basic_material(render::MaterialId::SIMPLE_SHADERS, false, false);

  // Create a triangle in the center-left
  triangle = create_triangle_2d("scene1_triangle", render::MaterialId::SIMPLE_SHADERS_2D,
                                glm::vec3(-0.3f, 0.0f, 0.0f),
                                glm::vec3(0.0f, 0.0f, 0.0f),
                                glm::vec3(0.3f, 0.3f, 1.0f));
  if (triangle) {
    triangle->set_rotation_mode(render::Object::RotationMode::SHADER_2D);
  }

  // Create a cube in the center-right
  cube = create_cube_3d("scene1_cube", render::MaterialId::SIMPLE_SHADERS,
                        glm::vec3(0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                        glm::vec3(0.25f, 0.25f, 0.25f));
  if (cube) {
    cube->set_rotation_mode(render::Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene 1 setup complete\n");
}

void Scene1::update(float deltaTime, float totalTime) {
  // Rotate triangle around Z-axis
  if (triangle) {
    triangle->rotate_2d(totalTime * 0.5f);
  }

  // Rotate cube in 3D
  if (cube) {
    cube->rotate(glm::vec3(totalTime * 0.3f, totalTime * 0.5f, totalTime * 0.7f));
  }
}

std::string Scene1::get_name() const { return "Scene 1: Basic Shapes"; }

} // namespace scene
