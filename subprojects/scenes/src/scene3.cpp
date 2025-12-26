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
                 "assets/textures/checkerboard.png");
  create_texture(render::TextureId::GRADIENT, "assets/textures/gradient.png");
  create_texture_atlas(render::TextureId::ATLAS, "assets/textures/atlas.png", 2,
                       2);

  // Create separate texture objects for each atlas region
  // These will be used by the region-specific materials
  create_atlas_region_texture(render::TextureId::ATLAS_0_0,
                              render::TextureId::ATLAS, 0, 0);
  create_atlas_region_texture(render::TextureId::ATLAS_0_1,
                              render::TextureId::ATLAS, 0, 1);
  create_atlas_region_texture(render::TextureId::ATLAS_1_0,
                              render::TextureId::ATLAS, 1, 0);
  create_atlas_region_texture(render::TextureId::ATLAS_1_1,
                              render::TextureId::ATLAS, 1, 1);

  // Create textured materials for 2D
  create_textured_material(render::MaterialId::TEXTURED_CHECKERBOARD, true);
  create_textured_material(render::MaterialId::TEXTURED_GRADIENT, true);
  create_textured_material(render::MaterialId::TEXTURED_ATLAS, true);

  // Create textured materials for 3D
  create_textured_material(render::MaterialId::TEXTURED_3D_CHECKERBOARD, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_GRADIENT, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS, false);

  // Create atlas region materials (all use the same atlas texture)
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS_0_0, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS_0_1, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS_1_0, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_ATLAS_1_1, false);

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

  // Create a 3D cube with different materials on each face using submeshes
  // Each face gets a different material/texture combination
  // No base texture - submeshes take precedence
  multiMaterialCube = create_cube_3d(
      "scene3_multi_material_cube",
      render::MaterialId::TEXTURED_3D_CHECKERBOARD,
      std::nullopt, // No base texture - overwrites material and submashes
      {
          // Front face - checkerboard - from base material
          {6, 6,
           render::MaterialId::TEXTURED_3D_GRADIENT}, // Back face - gradient
          {12, 6,
           render::MaterialId::TEXTURED_3D_ATLAS_0_0}, // Left face - atlas
                                                       // region (0,0)
          {18, 6,
           render::MaterialId::TEXTURED_3D_ATLAS_0_1}, // Right face - atlas
                                                       // region (0,1)
          {24, 6,
           render::MaterialId::TEXTURED_3D_ATLAS_1_0}, // Top face - atlas
                                                       // region (1,0)
          {30, 6,
           render::MaterialId::TEXTURED_3D_ATLAS_1_1}, // Bottom face - atlas
                                                       // region (1,1)
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
