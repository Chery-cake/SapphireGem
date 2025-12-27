#include "scene5.h"
#include <print>

scene::Scene5::Scene5(render::MaterialManager *matMgr,
                      render::TextureManager *texMgr,
                      device::BufferManager *bufMgr,
                      render::ObjectManager *objMgr)
    : render::Scene(matMgr, texMgr, bufMgr, objMgr), cube(nullptr) {}

void scene::Scene5::setup() {
  std::print("Setting up Scene 5: Multi-Shader Cube\n");

  // Create textures with different shapes for each face
  create_texture(render::TextureId::SCENE5_CIRCLE,
                 "assets/textures/layer_circle.png");
  create_texture(render::TextureId::SCENE5_STAR,
                 "assets/textures/layer_star.png");
  create_texture(render::TextureId::SCENE5_SQUARE,
                 "assets/textures/layer_square.png");
  create_texture(render::TextureId::SCENE5_TRIANGLE,
                 "assets/textures/layer_triangle.png");
  create_texture(render::TextureId::SCENE5_HEART,
                 "assets/textures/layer_heart.png");
  create_texture(
      render::TextureId::SCENE5_DIAMOND,
      "assets/textures/layer_circle.png"); // Reuse circle as diamond for now

  // Create materials for each face with different shaders
  // Each material uses the same textured3d shader but with different textures
  // In a more advanced version, each could use completely different shaders
  create_textured_material(render::MaterialId::SCENE5_FACE_0, false);
  create_textured_material(render::MaterialId::SCENE5_FACE_1, false);
  create_textured_material(render::MaterialId::SCENE5_FACE_2, false);
  create_textured_material(render::MaterialId::SCENE5_FACE_3, false);
  create_textured_material(render::MaterialId::SCENE5_FACE_4, false);
  create_textured_material(render::MaterialId::SCENE5_FACE_5, false);

  // Create cube with different materials for each face
  std::vector<render::Scene::SubmeshDef> cubeSubmeshes;

  // Face 0 (front) - Circle
  cubeSubmeshes.push_back({.indexStart = 0,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_0,
                           .textureId = render::TextureId::SCENE5_CIRCLE});

  // Face 1 (back) - Star
  cubeSubmeshes.push_back({.indexStart = 6,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_1,
                           .textureId = render::TextureId::SCENE5_STAR});

  // Face 2 (left) - Square
  cubeSubmeshes.push_back({.indexStart = 12,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_2,
                           .textureId = render::TextureId::SCENE5_SQUARE});

  // Face 3 (right) - Triangle
  cubeSubmeshes.push_back({.indexStart = 18,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_3,
                           .textureId = render::TextureId::SCENE5_TRIANGLE});

  // Face 4 (top) - Heart
  cubeSubmeshes.push_back({.indexStart = 24,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_4,
                           .textureId = render::TextureId::SCENE5_HEART});

  // Face 5 (bottom) - Diamond
  cubeSubmeshes.push_back({.indexStart = 30,
                           .indexCount = 6,
                           .materialId = render::MaterialId::SCENE5_FACE_5,
                           .textureId = render::TextureId::SCENE5_DIAMOND});

  // Create the cube
  cube = create_cube_3d(
      "scene5_cube", render::MaterialId::SCENE5_FACE_0,
      std::nullopt, // No base texture, each face has its own
      cubeSubmeshes, glm::vec3(0.0f, 0.0f, 0.0f), // Position at center
      glm::vec3(0.0f, 0.0f, 0.0f),                // Initial rotation
      glm::vec3(0.5f, 0.5f, 0.5f),                // Scale
      {},                                         // Default indices
      1.0f,                                       // Cube size
      {// Vertex colors (white for all)
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f),
       glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f), glm::vec3(1.0f)});

  if (cube) {
    cube->set_rotation_mode(render::Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene 5 setup complete - Cube with 6 different textured faces\n");
}

void scene::Scene5::update(float deltaTime, float totalTime) {
  // Rotate cube smoothly on all axes
  if (cube) {
    cube->rotate(
        glm::vec3(totalTime * 0.3f, totalTime * 0.5f, totalTime * 0.2f));
  }
}

std::string scene::Scene5::get_name() const {
  return "Scene 5: Multi-Shader 3D Cube";
}
