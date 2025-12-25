#include "scene3.h"
#include "identifiers.h"
#include <print>

scene::Scene3::Scene3(render::MaterialManager *matMgr,
                      render::TextureManager *texMgr,
                      device::BufferManager *bufMgr,
                      render::ObjectManager *objMgr)
    : render::Scene(matMgr, texMgr, bufMgr, objMgr), multiMaterialQuad(nullptr),
      multiMaterialCube(nullptr) {}

void scene::Scene3::setup() {
  std::print("Setting up Scene 3: Multi-Material Objects\n");

  // Create textures
  create_texture(render::TextureId::CHECKERBOARD,
                 "../assets/textures/checkerboard.png");
  create_texture(render::TextureId::GRADIENT,
                 "../assets/textures/gradient.png");
  create_texture_atlas(render::TextureId::ATLAS, "../assets/textures/atlas.png",
                       2, 2);

  // Create textured materials for 2D
  create_textured_material(render::MaterialId::TEXTURED_CHECKERBOARD, true);
  create_textured_material(render::MaterialId::TEXTURED_GRADIENT, true);
  create_textured_material(render::MaterialId::TEXTURED_ATLAS, true);

  // Create textured materials for 3D
  create_textured_material(render::MaterialId::TEXTURED_3D_CHECKERBOARD, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_GRADIENT, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS, false);

  // Create a 2D quad with two different materials (split horizontally)
  multiMaterialQuad =
      create_quad_2d("scene3_multi_material_quad",
                     render::MaterialId::TEXTURED_CHECKERBOARD, std::nullopt,
                     {
                         {0, 6, render::MaterialId::TEXTURED_CHECKERBOARD},
                         {6, 6, render::MaterialId::TEXTURED_GRADIENT},
                     },
                     glm::vec3(-0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                     glm::vec3(0.3f, 0.3f, 1.0f));

  // Create a 3D cube with different materials on each face
  multiMaterialCube =
      create_cube_3d("scene3_multi_material_cube",
                     render::MaterialId::TEXTURED_3D_CHECKERBOARD,
                     render::TextureId::CHECKERBOARD,
                     {
                         {0, 6, render::MaterialId::TEXTURED_3D_CHECKERBOARD},
                         {6, 6, render::MaterialId::TEXTURED_3D_GRADIENT},
                         {12, 6, render::MaterialId::TEXTURED_3D_ATLAS},
                         {18, 6, render::MaterialId::TEXTURED_3D_CHECKERBOARD},
                         {24, 6, render::MaterialId::TEXTURED_3D_GRADIENT},
                         {30, 6, render::MaterialId::TEXTURED_3D_ATLAS},
                     },
                     glm::vec3(0.3f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                     glm::vec3(0.25f, 0.25f, 0.25f));

  if (multiMaterialCube) {
    multiMaterialCube->set_rotation_mode(
        render::Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene 3 setup complete\n");
}

void scene::Scene3::update(float deltaTime, float totalTime) {
  // Rotate the multi-material cube to show all faces
  if (multiMaterialCube) {
    multiMaterialCube->rotate(
        glm::vec3(totalTime * 0.4f, totalTime * 0.6f, totalTime * 0.5f));
  }
}

std::string scene::Scene3::get_name() const {
  return "Scene 3: Multi-Material Objects";
}
