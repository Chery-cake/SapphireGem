#include "scene4.h"
#include <print>

scene::Scene4::Scene4(render::MaterialManager *matMgr,
                      render::TextureManager *texMgr,
                      device::BufferManager *bufMgr,
                      render::ObjectManager *objMgr)
    : render::Scene(matMgr, texMgr, bufMgr, objMgr), quad(nullptr),
      cube(nullptr) {}

void scene::Scene4::setup() {
  std::print("Setting up Scene 4: Layered Textures\n");

  // Create layered textures with transparent images
  // Quad with 3 layers - using transparent shapes
  create_layered_texture(
      render::TextureId::LAYERED_QUAD,
      {"../assets/textures/layer_circle.png",
       "../assets/textures/layer_star.png",
       "../assets/textures/layer_triangle.png"},
      {glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), // No tint, full opacity
       glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)},
      {0.0f, 0.0f, 0.0f}); // No rotation

  // Cube textures with incrementing layer counts (1-5 layers)
  // Face 1: 1 layer - single circle
  create_layered_texture(render::TextureId::LAYERED_CUBE_1,
                         {"../assets/textures/layer_circle.png"},
                         {glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)}, {0.0f});

  // Face 2: 2 layers - circle + star
  create_layered_texture(
      render::TextureId::LAYERED_CUBE_2,
      {"../assets/textures/layer_circle.png",
       "../assets/textures/layer_star.png"},
      {glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)},
      {0.0f, 45.0f});

  // Face 3: 3 layers - circle + star + square
  create_layered_texture(render::TextureId::LAYERED_CUBE_3,
                         {"../assets/textures/layer_circle.png",
                          "../assets/textures/layer_star.png",
                          "../assets/textures/layer_square.png"},
                         {glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                          glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                          glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)},
                         {0.0f, 0.0f, 90.0f});

  // Face 4: 4 layers - using atlas region (reused) + individual shapes
  create_layered_texture(
      render::TextureId::LAYERED_CUBE_4,
      {"../assets/textures/layer_circle.png",
       "../assets/textures/layer_star.png",
       "../assets/textures/layer_triangle.png",
       "../assets/textures/layer_heart.png"},
      {glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
       glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), glm::vec4(1.0f, 1.0f, 1.0f, 1.0f)},
      {0.0f, 30.0f, 60.0f, 90.0f});

  // Face 5: 5 layers - reusing same image with different modifications
  create_layered_texture(
      render::TextureId::LAYERED_CUBE_5,
      {"../assets/textures/layer_circle.png",
       "../assets/textures/layer_star.png",
       "../assets/textures/layer_square.png",
       "../assets/textures/layer_triangle.png",
       "../assets/textures/layer_heart.png"},
      {glm::vec4(1.0f, 0.8f, 0.8f, 0.9f),  // Slight red tint
       glm::vec4(0.8f, 1.0f, 0.8f, 0.8f),  // Slight green tint
       glm::vec4(0.8f, 0.8f, 1.0f, 0.7f),  // Slight blue tint
       glm::vec4(1.0f, 0.8f, 1.0f, 0.6f),  // Slight magenta tint
       glm::vec4(1.0f, 1.0f, 0.8f, 0.5f)}, // Slight yellow tint
      {0.0f, 36.0f, 72.0f, 108.0f, 144.0f});

  // NOTE: The layered materials should use the layered.spv shader which needs
  // to be compiled For now, using the standard textured materials as fallback
  // TODO: Compile layered.slang and update materials to use LAYERED_2D and
  // LAYERED_3D

  create_textured_material(render::MaterialId::TEXTURED, true);
  
  // Create separate materials for each layered texture cube face
  // This follows the same pattern as Scene3 with atlas regions
  create_textured_material(render::MaterialId::TEXTURED_3D_LAYERED_CUBE_1, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_LAYERED_CUBE_2, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_LAYERED_CUBE_3, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_LAYERED_CUBE_4, false);
  create_textured_material(render::MaterialId::TEXTURED_3D_LAYERED_CUBE_5, false);

  // Create material for face without texture (SIMPLE_SHADERS_3D_TEXTURED)
  // This material works with textured vertices but doesn't require a texture
  create_textured_material(render::MaterialId::SIMPLE_SHADERS_3D_TEXTURED,
                           false);

  // Create quad with 3-layer texture (on the left)
  quad = create_quad_2d(
      "scene4_layered_quad", render::MaterialId::TEXTURED,
      render::TextureId::LAYERED_QUAD, {}, glm::vec3(-0.5f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.4f, 0.4f, 1.0f));
  if (quad) {
    quad->set_rotation_mode(render::Object::RotationMode::SHADER_2D);
  }

  // Create cube with incrementing layer count per face (on the right)
  // Face 0: no texture (use SIMPLE_SHADERS material for this face)
  // Faces 1-5: use LAYERED_CUBE_1 through LAYERED_CUBE_5

  // For simplicity, we'll create the cube with submeshes for each face
  // Each face will have a different texture with increasing layer count
  std::vector<render::Scene::SubmeshDef> cubeSubmeshes;

  // Face 0 (front) - no texture, will use base material
  // Indices 0-5 (first 6 indices of the cube)

  // Face 1 (back) - 1 layer
  cubeSubmeshes.push_back(
      {.indexStart = 6,
       .indexCount = 6,
       .materialId = render::MaterialId::TEXTURED_3D_LAYERED_CUBE_1,
       .textureId = render::TextureId::LAYERED_CUBE_1});

  // Face 2 (left) - 2 layers
  cubeSubmeshes.push_back(
      {.indexStart = 12,
       .indexCount = 6,
       .materialId = render::MaterialId::TEXTURED_3D_LAYERED_CUBE_2,
       .textureId = render::TextureId::LAYERED_CUBE_2});

  // Face 3 (right) - 3 layers
  cubeSubmeshes.push_back(
      {.indexStart = 18,
       .indexCount = 6,
       .materialId = render::MaterialId::TEXTURED_3D_LAYERED_CUBE_3,
       .textureId = render::TextureId::LAYERED_CUBE_3});

  // Face 4 (top) - 4 layers
  cubeSubmeshes.push_back(
      {.indexStart = 24,
       .indexCount = 6,
       .materialId = render::MaterialId::TEXTURED_3D_LAYERED_CUBE_4,
       .textureId = render::TextureId::LAYERED_CUBE_4});

  // Face 5 (bottom) - 5 layers
  cubeSubmeshes.push_back(
      {.indexStart = 30,
       .indexCount = 6,
       .materialId = render::MaterialId::TEXTURED_3D_LAYERED_CUBE_5,
       .textureId = render::TextureId::LAYERED_CUBE_5});

  cube = create_cube_3d(
      "scene4_layered_cube", render::MaterialId::SIMPLE_SHADERS_3D_TEXTURED,
      std::nullopt, cubeSubmeshes, glm::vec3(0.5f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.3f, 0.3f, 0.3f), {}, 1.0f,
      {// Front face (no texture): white
       glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f),
       glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f),
       // Other faces will use textures
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f)});

  if (cube) {
    cube->set_rotation_mode(render::Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene 4 setup complete\n");
}

void scene::Scene4::update(float deltaTime, float totalTime) {
  // Rotate quad gently
  if (quad) {
    quad->rotate_2d(totalTime * 0.3f);
  }

  // Rotate cube in 3D
  if (cube) {
    cube->rotate(
        glm::vec3(totalTime * 0.2f, totalTime * 0.4f, totalTime * 0.3f));
  }
}

std::string scene::Scene4::get_name() const {
  return "Scene 4: Layered Textures";
}
