#include "scene3.h"
#include "texture.h"
#include <print>

namespace render {

Scene3::Scene3(MaterialManager *matMgr, TextureManager *texMgr,
               device::BufferManager *bufMgr, ObjectManager *objMgr)
    : Scene(matMgr, texMgr, bufMgr, objMgr), multiMaterialQuad(nullptr),
      multiMaterialCube(nullptr) {}

void Scene3::setup() {
  std::print("Setting up Scene 3: Multi-Material Objects\n");

  // Create textures
  create_texture(TextureId::CHECKERBOARD, "../assets/textures/checkerboard.png");
  create_texture(TextureId::GRADIENT, "../assets/textures/gradient.png");
  create_texture_atlas(TextureId::ATLAS, "../assets/textures/atlas.png", 2, 2);

  // Create textured materials for 2D
  create_textured_material(MaterialId::TEXTURED_CHECKERBOARD, true);
  create_textured_material(MaterialId::TEXTURED_GRADIENT, true);
  create_textured_material(MaterialId::TEXTURED_ATLAS, true);

  // Create textured materials for 3D
  create_textured_material(MaterialId::TEXTURED_3D_CHECKERBOARD, false);
  create_textured_material(MaterialId::TEXTURED_3D_GRADIENT, false);
  create_textured_material(MaterialId::TEXTURED_3D_ATLAS, false);

  // Create basic 3D material for one face
  create_basic_material(MaterialId::SIMPLE_SHADERS_3D_TEXTURED, false, true);

  // Create a 2D quad with two different materials (split horizontally)
  multiMaterialQuad = create_multi_material_quad_2d(
      "scene3_multi_material_quad",
      {MaterialId::TEXTURED_CHECKERBOARD, MaterialId::TEXTURED_GRADIENT},
      {TextureId::CHECKERBOARD, TextureId::GRADIENT},
      glm::vec3(-0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.3f, 0.3f, 1.0f));

  // Create a 3D cube with different materials on each face
  multiMaterialCube = create_multi_material_cube_3d(
      "scene3_multi_material_cube",
      {MaterialId::TEXTURED_3D_CHECKERBOARD, MaterialId::TEXTURED_3D_GRADIENT,
       MaterialId::TEXTURED_3D_ATLAS, MaterialId::SIMPLE_SHADERS_3D_TEXTURED,
       MaterialId::TEXTURED_3D_CHECKERBOARD, MaterialId::TEXTURED_3D_GRADIENT},
      {TextureId::CHECKERBOARD, TextureId::GRADIENT, TextureId::ATLAS,
       TextureId::CHECKERBOARD, TextureId::CHECKERBOARD, TextureId::GRADIENT},
      glm::vec3(0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.25f, 0.25f, 0.25f));

  if (multiMaterialCube) {
    multiMaterialCube->set_rotation_mode(Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene 3 setup complete\n");
}

void Scene3::update(float deltaTime, float totalTime) {
  // Rotate the multi-material cube to show all faces
  if (multiMaterialCube) {
    multiMaterialCube->rotate(
        glm::vec3(totalTime * 0.4f, totalTime * 0.6f, totalTime * 0.5f));
  }
}

std::string Scene3::get_name() const {
  return "Scene 3: Multi-Material Objects";
}

} // namespace render
