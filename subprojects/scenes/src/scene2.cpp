#include "scene2.h"
#include <print>

scene::Scene2::Scene2(render::MaterialManager *matMgr,
                      render::TextureManager *texMgr,
                      device::BufferManager *bufMgr,
                      render::ObjectManager *objMgr)
    : render::Scene(matMgr, texMgr, bufMgr, objMgr), texturedSquare(nullptr),
      imageQuad(nullptr), atlasQuad1(nullptr), atlasQuad2(nullptr),
      atlasQuad3(nullptr), atlasQuad4(nullptr) {}

void scene::Scene2::setup() {
  std::print("Setting up Scene 2: Textured Objects\n");

  // Create textures
  create_texture(render::TextureId::CHECKERBOARD,
                 "../assets/textures/checkerboard.png");
  create_texture(render::TextureId::GRADIENT,
                 "../assets/textures/gradient.png");
  create_texture_atlas(render::TextureId::ATLAS, "../assets/textures/atlas.png",
                       2, 2);

  // Apply texture modifications
  auto *checkerboardTex =
      textureManager->get_texture(to_string(render::TextureId::CHECKERBOARD));
  if (checkerboardTex) {
    checkerboardTex->set_color_tint(glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));
    checkerboardTex->update_gpu();
  }

  auto *gradientTex =
      textureManager->get_texture(to_string(render::TextureId::GRADIENT));
  if (gradientTex) {
    gradientTex->rotate_90_clockwise();
    gradientTex->update_gpu();
  }

  // Create textured materials
  create_textured_material(render::MaterialId::TEXTURED_CHECKERBOARD, true);
  create_textured_material(render::MaterialId::TEXTURED_GRADIENT, true);
  create_textured_material(render::MaterialId::TEXTURED_ATLAS, true);

  // Create textured objects
  texturedSquare = create_textured_quad_2d(
      "scene2_textured_square", render::MaterialId::TEXTURED_CHECKERBOARD,
      render::TextureId::CHECKERBOARD, glm::vec3(-0.5f, 0.4f, 0.0f),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.2f, 0.2f, 1.0f));

  imageQuad = create_textured_quad_2d(
      "scene2_image_quad", render::MaterialId::TEXTURED_GRADIENT,
      render::TextureId::GRADIENT, glm::vec3(-0.2f, 0.4f, 0.0f),
      glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.2f, 0.2f, 1.0f));

  // Create atlas quads with custom UV coordinates
  constexpr float atlasHalfU = 0.5f;
  constexpr float atlasHalfV = 0.5f;

  // Atlas quad 1 - Top-left region
  {
    const std::vector<render::Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {atlasHalfU, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {0.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}}};
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};

    render::Object::ObjectCreateInfo createInfo{
        .identifier = "scene2_atlas_quad1",
        .type = render::Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = to_string(render::MaterialId::TEXTURED_ATLAS),
        .textureIdentifier = to_string(render::TextureId::ATLAS),
        .position = glm::vec3(0.2f, 0.3f, 0.0f),
        .scale = glm::vec3(0.15f, 0.15f, 1.0f),
        .visible = true};
    atlasQuad1 = objectManager->create_object(createInfo);
    if (atlasQuad1) {
      sceneObjects.push_back(atlasQuad1);
    }
  }

  // Atlas quad 2 - Top-right region
  {
    const std::vector<render::Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {atlasHalfU, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}}};
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};

    render::Object::ObjectCreateInfo createInfo{
        .identifier = "scene2_atlas_quad2",
        .type = render::Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = to_string(render::MaterialId::TEXTURED_ATLAS),
        .textureIdentifier = to_string(render::TextureId::ATLAS),
        .position = glm::vec3(0.5f, 0.3f, 0.0f),
        .scale = glm::vec3(0.15f, 0.15f, 1.0f),
        .visible = true};
    atlasQuad2 = objectManager->create_object(createInfo);
    if (atlasQuad2) {
      sceneObjects.push_back(atlasQuad2);
    }
  }

  // Atlas quad 3 - Bottom-left region
  {
    const std::vector<render::Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {0.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {atlasHalfU, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}};
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};

    render::Object::ObjectCreateInfo createInfo{
        .identifier = "scene2_atlas_quad3",
        .type = render::Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = to_string(render::MaterialId::TEXTURED_ATLAS),
        .textureIdentifier = to_string(render::TextureId::ATLAS),
        .position = glm::vec3(0.2f, -0.1f, 0.0f),
        .scale = glm::vec3(0.15f, 0.15f, 1.0f),
        .visible = true};
    atlasQuad3 = objectManager->create_object(createInfo);
    if (atlasQuad3) {
      sceneObjects.push_back(atlasQuad3);
    }
  }

  // Atlas quad 4 - Bottom-right region
  {
    const std::vector<render::Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {1.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {atlasHalfU, 1.0f}, {1.0f, 1.0f, 1.0f}}};
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};

    render::Object::ObjectCreateInfo createInfo{
        .identifier = "scene2_atlas_quad4",
        .type = render::Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = to_string(render::MaterialId::TEXTURED_ATLAS),
        .textureIdentifier = to_string(render::TextureId::ATLAS),
        .position = glm::vec3(0.5f, -0.1f, 0.0f),
        .scale = glm::vec3(0.15f, 0.15f, 1.0f),
        .visible = true};
    atlasQuad4 = objectManager->create_object(createInfo);
    if (atlasQuad4) {
      sceneObjects.push_back(atlasQuad4);
    }
  }

  std::print("Scene 2 setup complete\n");
}

void scene::Scene2::update(float deltaTime, float totalTime) {
  // Gentle rotation for textured objects
  if (texturedSquare) {
    texturedSquare->rotate_2d(totalTime * 0.5f);
  }

  if (imageQuad) {
    imageQuad->rotate_2d(totalTime * 0.8f);
  }
}

std::string scene::Scene2::get_name() const {
  return "Scene 2: Textured Objects";
}
