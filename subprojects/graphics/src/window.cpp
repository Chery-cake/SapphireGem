#include "window.h"
#include "object.h"
#include "renderer.h"
#include "texture.h"
#include "texture_manager.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <memory>
#include <print>
#include <stdexcept>

render::Window::Window(int width, int height, std::string title)
    : frameBufferRezized(false), triangle(nullptr), cube(nullptr),
      texturedSquare(nullptr), imageQuad(nullptr), atlasQuad1(nullptr),
      atlasQuad2(nullptr), atlasQuad3(nullptr), atlasQuad4(nullptr),
      multiMaterialQuad(nullptr), multiMaterialCube(nullptr),
      currentWidth(width), currentHeight(height),
      aspectRatio(static_cast<float>(width) / static_cast<float>(height)) {
  glfwInit();

  if (!glfwVulkanSupported()) {
    throw std::runtime_error("GLFW can't load Vulkan\n");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, frame_buffer_resize_callback);
  glfwSetKeyCallback(window, key_callback);

  renderer = std::make_unique<Renderer>(window);

  create_scene_objects();
}

render::Window::~Window() {
  renderer.reset();

  glfwDestroyWindow(window);

  glfwTerminate();

  std::print("Window destructor executed\n");
}

void render::Window::frame_buffer_resize_callback(GLFWwindow *window, int width,
                                                  int height) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));
  win->frameBufferRezized = true;
  win->currentWidth = width;
  win->currentHeight = height;
  win->aspectRatio = static_cast<float>(width) / static_cast<float>(height);
  win->update_object_positions();
}

void render::Window::key_callback(GLFWwindow *window, int key, int scancode,
                                  int action, int mods) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));

  // Reserved for future keyboard input handling
}

void render::Window::run() {
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (frameBufferRezized) {
      frameBufferRezized = false;
      renderer->get_device_manager().recreate_swap_chain();
    }

    update_scene_objects();

    renderer->draw_frame();
  };
}

void render::Window::create_scene_objects() {
  if (!renderer) {
    std::print("Error: Renderer not initialized\n");
    return;
  }

  // Load textures using the TextureManager
  auto &textureMgr = renderer->get_texture_manager();
  auto &materialMgr = renderer->get_material_manager();
  auto &bufferMgr = renderer->get_buffer_manager();
  
  std::print("Loading textures...\n");

  // Load checkerboard texture
  auto *checkerboardTex = textureMgr.create_texture(
      "checkerboard", "../assets/textures/checkerboard.png");
  if (checkerboardTex) {
    std::print("Loaded checkerboard texture: {}x{}\n",
               checkerboardTex->get_width(), checkerboardTex->get_height());
    checkerboardTex->set_color_tint(glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));
    checkerboardTex->update_gpu();
  }

  // Load gradient texture
  auto *gradientTex =
      textureMgr.create_texture("gradient", "../assets/textures/gradient.png");
  if (gradientTex) {
    std::print("Loaded gradient texture: {}x{}\n", gradientTex->get_width(),
               gradientTex->get_height());
    gradientTex->rotate_90_clockwise();
    gradientTex->update_gpu();
  }
  
  // Load texture atlas (2x2 grid)
  auto *atlasTex = textureMgr.create_texture_atlas(
      "atlas", "../assets/textures/atlas.png", 2, 2);
  if (atlasTex) {
    std::print("Loaded texture atlas: {}x{}\n",
               atlasTex->get_width(), atlasTex->get_height());
  }

  // Setup common material bindings
  device::Buffer::TransformUBO uboData = {
      .model = glm::mat4(1.0f),
      .view = glm::mat4(1.0f),
      .proj = glm::mat4(1.0f)
  };

  vk::DescriptorSetLayoutBinding uboBinding = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eVertex};

  vk::DescriptorSetLayoutBinding samplerBinding = {
      .binding = 1,
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eFragment};

  auto texturedBindingDescription =
      Object::Vertex2DTextured::getBindingDescription();
  auto texturedAttributeDescriptions =
      Object::Vertex2DTextured::getAttributeDescriptions();

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  // Create material for checkerboard texture (if not exists)
  if (!materialMgr.get_material("Textured_checkerboard")) {
    Material::MaterialCreateInfo checkerboardMaterialInfo{
        .identifier = "Textured_checkerboard",
        .vertexShaders = "../assets/shaders/textured.spv",
        .fragmentShaders = "../assets/shaders/textured.spv",
        .descriptorBindings = {uboBinding, samplerBinding},
        .rasterizationState = {.depthClampEnable = vk::False,
                               .rasterizerDiscardEnable = vk::False,
                               .polygonMode = vk::PolygonMode::eFill,
                               .cullMode = vk::CullModeFlagBits::eBack,
                               .frontFace = vk::FrontFace::eCounterClockwise,
                               .depthBiasEnable = vk::False,
                               .depthBiasSlopeFactor = 1.0f,
                               .lineWidth = 1.0f},
        .depthStencilState = {},
        .blendState = {.logicOpEnable = vk::False,
                       .logicOp = vk::LogicOp::eCopy,
                       .attachmentCount = 1,
                       .pAttachments = &colorBlendAttachment},
        .vertexInputState{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &texturedBindingDescription,
            .vertexAttributeDescriptionCount =
                static_cast<uint32_t>(texturedAttributeDescriptions.size()),
            .pVertexAttributeDescriptions =
                texturedAttributeDescriptions.data()},
        .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
        .viewportState{.viewportCount = 1, .scissorCount = 1},
        .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                          .sampleShadingEnable = vk::False},
        .dynamicStates{vk::DynamicState::eViewport,
                       vk::DynamicState::eScissor}};
    materialMgr.add_material(checkerboardMaterialInfo);
    
    device::Buffer::BufferCreateInfo uboInfo = {
        .identifier = "Textured_checkerboard_ubo",
        .type = device::Buffer::BufferType::UNIFORM,
        .usage = device::Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(device::Buffer::TransformUBO),
        .elementSize = sizeof(device::Buffer::TransformUBO),
        .initialData = &uboData};
    bufferMgr.create_buffer(uboInfo);
  }

  // Create material for gradient texture (if not exists)
  if (!materialMgr.get_material("Textured_gradient")) {
    Material::MaterialCreateInfo gradientMaterialInfo{
        .identifier = "Textured_gradient",
        .vertexShaders = "../assets/shaders/textured.spv",
        .fragmentShaders = "../assets/shaders/textured.spv",
        .descriptorBindings = {uboBinding, samplerBinding},
        .rasterizationState = {.depthClampEnable = vk::False,
                               .rasterizerDiscardEnable = vk::False,
                               .polygonMode = vk::PolygonMode::eFill,
                               .cullMode = vk::CullModeFlagBits::eBack,
                               .frontFace = vk::FrontFace::eCounterClockwise,
                               .depthBiasEnable = vk::False,
                               .depthBiasSlopeFactor = 1.0f,
                               .lineWidth = 1.0f},
        .depthStencilState = {},
        .blendState = {.logicOpEnable = vk::False,
                       .logicOp = vk::LogicOp::eCopy,
                       .attachmentCount = 1,
                       .pAttachments = &colorBlendAttachment},
        .vertexInputState{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &texturedBindingDescription,
            .vertexAttributeDescriptionCount =
                static_cast<uint32_t>(texturedAttributeDescriptions.size()),
            .pVertexAttributeDescriptions =
                texturedAttributeDescriptions.data()},
        .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
        .viewportState{.viewportCount = 1, .scissorCount = 1},
        .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                          .sampleShadingEnable = vk::False},
        .dynamicStates{vk::DynamicState::eViewport,
                       vk::DynamicState::eScissor}};
    materialMgr.add_material(gradientMaterialInfo);
    
    device::Buffer::BufferCreateInfo uboInfo = {
        .identifier = "Textured_gradient_ubo",
        .type = device::Buffer::BufferType::UNIFORM,
        .usage = device::Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(device::Buffer::TransformUBO),
        .elementSize = sizeof(device::Buffer::TransformUBO),
        .initialData = &uboData};
    bufferMgr.create_buffer(uboInfo);
  }
  
  // Create material for atlas texture (if not exists)
  if (!materialMgr.get_material("Textured_atlas")) {
    Material::MaterialCreateInfo atlasMaterialInfo{
        .identifier = "Textured_atlas",
        .vertexShaders = "../assets/shaders/textured.spv",
        .fragmentShaders = "../assets/shaders/textured.spv",
        .descriptorBindings = {uboBinding, samplerBinding},
        .rasterizationState = {.depthClampEnable = vk::False,
                               .rasterizerDiscardEnable = vk::False,
                               .polygonMode = vk::PolygonMode::eFill,
                               .cullMode = vk::CullModeFlagBits::eBack,
                               .frontFace = vk::FrontFace::eCounterClockwise,
                               .depthBiasEnable = vk::False,
                               .depthBiasSlopeFactor = 1.0f,
                               .lineWidth = 1.0f},
        .depthStencilState = {},
        .blendState = {.logicOpEnable = vk::False,
                       .logicOp = vk::LogicOp::eCopy,
                       .attachmentCount = 1,
                       .pAttachments = &colorBlendAttachment},
        .vertexInputState{
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &texturedBindingDescription,
            .vertexAttributeDescriptionCount =
                static_cast<uint32_t>(texturedAttributeDescriptions.size()),
            .pVertexAttributeDescriptions =
                texturedAttributeDescriptions.data()},
        .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
        .viewportState{.viewportCount = 1, .scissorCount = 1},
        .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                          .sampleShadingEnable = vk::False},
        .dynamicStates{vk::DynamicState::eViewport,
                       vk::DynamicState::eScissor}};
    materialMgr.add_material(atlasMaterialInfo);
    
    device::Buffer::BufferCreateInfo uboInfo = {
        .identifier = "Textured_atlas_ubo",
        .type = device::Buffer::BufferType::UNIFORM,
        .usage = device::Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(device::Buffer::TransformUBO),
        .elementSize = sizeof(device::Buffer::TransformUBO),
        .initialData = &uboData};
    bufferMgr.create_buffer(uboInfo);
  }
  
  // Create unique materials for each atlas quad to avoid descriptor set conflicts
  // Each quad needs its own material to properly bind the shared atlas texture
  constexpr int NUM_ATLAS_QUADS = 4;
  for (int i = 1; i <= NUM_ATLAS_QUADS; ++i) {
    std::string matName = "Textured_atlas_quad" + std::to_string(i);
    if (!materialMgr.get_material(matName)) {
      Material::MaterialCreateInfo atlasQuadMaterialInfo{
          .identifier = matName,
          .vertexShaders = "../assets/shaders/textured.spv",
          .fragmentShaders = "../assets/shaders/textured.spv",
          .descriptorBindings = {uboBinding, samplerBinding},
          .rasterizationState = {.depthClampEnable = vk::False,
                                 .rasterizerDiscardEnable = vk::False,
                                 .polygonMode = vk::PolygonMode::eFill,
                                 .cullMode = vk::CullModeFlagBits::eBack,
                                 .frontFace = vk::FrontFace::eCounterClockwise,
                                 .depthBiasEnable = vk::False,
                                 .depthBiasSlopeFactor = 1.0f,
                                 .lineWidth = 1.0f},
          .depthStencilState = {},
          .blendState = {.logicOpEnable = vk::False,
                         .logicOp = vk::LogicOp::eCopy,
                         .attachmentCount = 1,
                         .pAttachments = &colorBlendAttachment},
          .vertexInputState{
              .vertexBindingDescriptionCount = 1,
              .pVertexBindingDescriptions = &texturedBindingDescription,
              .vertexAttributeDescriptionCount =
                  static_cast<uint32_t>(texturedAttributeDescriptions.size()),
              .pVertexAttributeDescriptions =
                  texturedAttributeDescriptions.data()},
          .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
          .viewportState{.viewportCount = 1, .scissorCount = 1},
          .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                            .sampleShadingEnable = vk::False},
          .dynamicStates{vk::DynamicState::eViewport,
                         vk::DynamicState::eScissor}};
      materialMgr.add_material(atlasQuadMaterialInfo);
      
      std::string uboName = matName + "_ubo";
      device::Buffer::BufferCreateInfo uboInfo = {
          .identifier = uboName,
          .type = device::Buffer::BufferType::UNIFORM,
          .usage = device::Buffer::BufferUsage::DYNAMIC,
          .size = sizeof(device::Buffer::TransformUBO),
          .elementSize = sizeof(device::Buffer::TransformUBO),
          .initialData = &uboData};
      bufferMgr.create_buffer(uboInfo);
    }
  }

  // Bind textures to materials
  auto *checkerboardMaterial = materialMgr.get_material("Textured_checkerboard");
  if (checkerboardMaterial && checkerboardTex) {
    auto *uboBuffer = bufferMgr.get_buffer("Textured_checkerboard_ubo");
    if (uboBuffer) {
      checkerboardMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
    }
    checkerboardMaterial->bind_texture(checkerboardTex->get_image().get(), 1, 0);
    std::print("✓ Bound checkerboard texture\n");
  }

  auto *gradientMaterial = materialMgr.get_material("Textured_gradient");
  if (gradientMaterial && gradientTex) {
    auto *uboBuffer = bufferMgr.get_buffer("Textured_gradient_ubo");
    if (uboBuffer) {
      gradientMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
    }
    gradientMaterial->bind_texture(gradientTex->get_image().get(), 1, 0);
    std::print("✓ Bound gradient texture\n");
  }
  
  // Bind atlas texture to all atlas quad materials
  if (atlasTex) {
    // Bind to base atlas material (used by multi-material cube)
    auto *atlasMaterial = materialMgr.get_material("Textured_atlas");
    if (atlasMaterial) {
      auto *uboBuffer = bufferMgr.get_buffer("Textured_atlas_ubo");
      if (uboBuffer) {
        atlasMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
      }
      atlasMaterial->bind_texture(atlasTex->get_image().get(), 1, 0);
    }
    
    // Bind to individual atlas quad materials
    for (int i = 1; i <= NUM_ATLAS_QUADS; ++i) {
      std::string matName = "Textured_atlas_quad" + std::to_string(i);
      auto *atlasQuadMaterial = materialMgr.get_material(matName);
      if (atlasQuadMaterial) {
        std::string uboName = matName + "_ubo";
        auto *uboBuffer = bufferMgr.get_buffer(uboName);
        if (uboBuffer) {
          atlasQuadMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
        }
        atlasQuadMaterial->bind_texture(atlasTex->get_image().get(), 1, 0);
      }
    }
    std::print("✓ Bound atlas texture to base material and {} quad materials\n", NUM_ATLAS_QUADS);
  }

  // Create original objects - positions will be updated by update_object_positions()
  std::print("Creating original objects...\n");
  texturedSquare = renderer->create_textured_square_2d(
      "textured_square", "checkerboard",
      glm::vec3(0.0f, 0.0f, 0.0f),  // Temp position
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.15f, 0.15f, 1.0f));

  imageQuad = renderer->create_textured_square_2d(
      "image_quad", "gradient",
      glm::vec3(0.0f, 0.0f, 0.0f),  // Temp position
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.15f, 0.15f, 1.0f));

  triangle = renderer->create_triangle_2d(
      "triangle",
      glm::vec3(0.0f, 0.0f, 0.0f),  // Temp position
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.2f, 0.2f, 1.0f));
  if (triangle) {
    triangle->set_rotation_mode(Object::RotationMode::SHADER_2D);
  }

  cube = renderer->create_cube_3d(
      "cube",
      glm::vec3(0.0f, 0.0f, 0.0f),  // Temp position
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.15f, 0.15f, 0.15f));
  if (cube) {
    cube->set_rotation_mode(Object::RotationMode::TRANSFORM_3D);
  }

  // Create atlas quads with UV coordinates for each region
  std::print("Creating atlas quads...\n");
  
  // Define UV bounds for the 2x2 atlas grid
  // Each region is 0.5 x 0.5 in UV space (50% of texture width/height)
  constexpr float atlasHalfU = 0.5f; // Midpoint of U coordinate (horizontal)
  constexpr float atlasHalfV = 0.5f; // Midpoint of V coordinate (vertical)
  
  // Atlas quad 1 - Top-left (Red) region (UV: 0.0-0.5, 0.0-0.5)
  {
    const std::vector<Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {atlasHalfU, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {0.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};
    
    Object::ObjectCreateInfo createInfo{
        .identifier = "atlas_quad1",
        .type = Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = "Textured_atlas_quad1",
        .textureIdentifier = "atlas",
        .position = glm::vec3(0.0f, 0.0f, 0.0f),
        .scale = glm::vec3(0.12f, 0.12f, 1.0f),
        .visible = true
    };
    atlasQuad1 = renderer->get_object_manager()->create_object(createInfo);
  }
  
  // Atlas quad 2 - Top-right (Green) region (UV: 0.5-1.0, 0.0-0.5)
  {
    const std::vector<Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {atlasHalfU, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};
    
    Object::ObjectCreateInfo createInfo{
        .identifier = "atlas_quad2",
        .type = Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = "Textured_atlas_quad2",
        .textureIdentifier = "atlas",
        .position = glm::vec3(0.0f, 0.0f, 0.0f),
        .scale = glm::vec3(0.12f, 0.12f, 1.0f),
        .visible = true
    };
    atlasQuad2 = renderer->get_object_manager()->create_object(createInfo);
  }
  
  // Atlas quad 3 - Bottom-left (Blue) region (UV: 0.0-0.5, 0.5-1.0)
  {
    const std::vector<Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {0.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {atlasHalfU, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};
    
    Object::ObjectCreateInfo createInfo{
        .identifier = "atlas_quad3",
        .type = Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = "Textured_atlas_quad3",
        .textureIdentifier = "atlas",
        .position = glm::vec3(0.0f, 0.0f, 0.0f),
        .scale = glm::vec3(0.12f, 0.12f, 1.0f),
        .visible = true
    };
    atlasQuad3 = renderer->get_object_manager()->create_object(createInfo);
  }
  
  // Atlas quad 4 - Bottom-right (Yellow) region (UV: 0.5-1.0, 0.5-1.0)
  {
    const std::vector<Object::Vertex2DTextured> vertices = {
        {{-0.5f, -0.5f}, {atlasHalfU, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {1.0f, atlasHalfV}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {atlasHalfU, 1.0f}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {0, 2, 1, 0, 3, 2};
    
    Object::ObjectCreateInfo createInfo{
        .identifier = "atlas_quad4",
        .type = Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = "Textured_atlas_quad4",
        .textureIdentifier = "atlas",
        .position = glm::vec3(0.0f, 0.0f, 0.0f),
        .scale = glm::vec3(0.12f, 0.12f, 1.0f),
        .visible = true
    };
    atlasQuad4 = renderer->get_object_manager()->create_object(createInfo);
  }
  
  // Create a 2D quad with submeshes using both materials
  std::print("Creating multi-material 2D quad...\n");
  {
    const std::vector<Object::Vertex2DTextured> vertices = {
        // Left half (for checkerboard)
        {{-0.5f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
        // Right half (for gradient)
        {{0.0f, -0.5f}, {0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, -0.5f}, {1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.5f, 0.5f}, {1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}},
        {{0.0f, 0.5f}, {0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}}
    };
    const std::vector<uint16_t> indices = {
        0, 2, 1, 0, 3, 2,  // Left triangle pair (checkerboard)
        4, 6, 5, 4, 7, 6   // Right triangle pair (gradient)
    };
    
    // Define submeshes: Each quad uses 6 indices (2 triangles)
    std::vector<Object::Submesh> submeshes = {
        {0, 6, "Textured_checkerboard", nullptr},  // Start at index 0, use 6 indices
        {6, 6, "Textured_gradient", nullptr}       // Start at index 6, use 6 indices
    };
    
    Object::ObjectCreateInfo createInfo{
        .identifier = "multi_material_quad",
        .type = Object::ObjectType::OBJECT_2D,
        .vertices = vertices,
        .indices = indices,
        .materialIdentifier = "Textured_checkerboard",
        .submeshes = submeshes,
        .position = glm::vec3(0.0f, 0.0f, 0.0f),
        .scale = glm::vec3(0.2f, 0.2f, 1.0f),
        .visible = true
    };
    multiMaterialQuad = renderer->get_object_manager()->create_object(createInfo);
  }
  
  // Create a multi-material 3D cube
  std::print("Creating multi-material 3D cube...\n");
  multiMaterialCube = renderer->create_multi_material_cube_3d(
      "multi_material_cube",
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.0f, 0.0f, 0.0f),
      glm::vec3(0.2f, 0.2f, 0.2f));
  if (multiMaterialCube) {
    multiMaterialCube->set_rotation_mode(Object::RotationMode::TRANSFORM_3D);
  }

  // Set initial positions based on aspect ratio
  update_object_positions();

  std::print("All scene objects created successfully\n");
}

void render::Window::update_scene_objects() {
  static double lastTime = glfwGetTime();
  double currentTime = glfwGetTime();
  float deltaTime = static_cast<float>(currentTime - lastTime);
  lastTime = currentTime;

  static float time = 0.0f;
  time += deltaTime;

  if (triangle) {
    // Rotate triangle in Z axis only (in-plane 2D rotation)
    triangle->rotate_2d(time * 0.5f);
  }

  if (cube) {
    // Rotate cube in X, Y, and Z axes simultaneously (3D rotation mode)
    cube->rotate(glm::vec3(time * 0.3f, time * 0.5f, time * 0.7f));
  }

  if (texturedSquare) {
    // Gentle rotation for textured square (shader-based 2D rotation)
    texturedSquare->rotate_2d(time * 0.5f);
  }

  if (imageQuad) {
    // Slightly faster rotation for image quad (shader-based 2D rotation)
    imageQuad->rotate_2d(time * 0.8f);
  }
  
  if (multiMaterialCube) {
    // Rotate multi-material cube to show all faces
    multiMaterialCube->rotate(glm::vec3(time * 0.4f, time * 0.6f, time * 0.5f));
  }
}

void render::Window::update_object_positions() {
  // Calculate scale factors to maintain aspect ratio
  // Objects should maintain their square aspect ratio regardless of window shape
  
  // Position scaling: compress positions in the longer axis
  float scaleX = aspectRatio > 1.0f ? (1.0f / aspectRatio) : 1.0f;
  float scaleY = aspectRatio < 1.0f ? aspectRatio : 1.0f;
  
  // Object scale compensation: expand objects in the compressed axis to prevent stretching
  float objectScaleX = std::max(aspectRatio, 1.0f);
  float objectScaleY = std::max(1.0f / aspectRatio, 1.0f);
  
  // Position objects in a grid layout that adapts to window size
  float topY = 0.65f * scaleY;
  float midY = 0.0f;
  float botY = -0.65f * scaleY;
  
  float leftX = -0.7f * scaleX;
  float midLeftX = -0.35f * scaleX;
  float midX = 0.0f;
  float midRightX = 0.35f * scaleX;
  float rightX = 0.7f * scaleX;
  
  // Position original objects with scale compensation
  if (texturedSquare) {
    texturedSquare->set_position(glm::vec3(leftX, topY, 0.0f));
    texturedSquare->set_scale(glm::vec3(0.15f * objectScaleX, 0.15f * objectScaleY, 1.0f));
  }
  
  if (imageQuad) {
    imageQuad->set_position(glm::vec3(midLeftX, topY, 0.0f));
    imageQuad->set_scale(glm::vec3(0.15f * objectScaleX, 0.15f * objectScaleY, 1.0f));
  }
  
  if (triangle) {
    triangle->set_position(glm::vec3(leftX, botY, 0.0f));
    triangle->set_scale(glm::vec3(0.2f * objectScaleX, 0.2f * objectScaleY, 1.0f));
  }
  
  if (cube) {
    cube->set_position(glm::vec3(midLeftX, botY, 0.0f));
    cube->set_scale(glm::vec3(0.15f * objectScaleX, 0.15f * objectScaleY, 0.15f));
  }
  
  // Position atlas quads in a grid with scale compensation
  if (atlasQuad1) {
    atlasQuad1->set_position(glm::vec3(midRightX, topY, 0.0f));
    atlasQuad1->set_scale(glm::vec3(0.12f * objectScaleX, 0.12f * objectScaleY, 1.0f));
  }
  
  if (atlasQuad2) {
    atlasQuad2->set_position(glm::vec3(rightX, topY, 0.0f));
    atlasQuad2->set_scale(glm::vec3(0.12f * objectScaleX, 0.12f * objectScaleY, 1.0f));
  }
  
  if (atlasQuad3) {
    atlasQuad3->set_position(glm::vec3(midRightX, midY, 0.0f));
    atlasQuad3->set_scale(glm::vec3(0.12f * objectScaleX, 0.12f * objectScaleY, 1.0f));
  }
  
  if (atlasQuad4) {
    atlasQuad4->set_position(glm::vec3(rightX, midY, 0.0f));
    atlasQuad4->set_scale(glm::vec3(0.12f * objectScaleX, 0.12f * objectScaleY, 1.0f));
  }
  
  // Position multi-material quad with scale compensation
  if (multiMaterialQuad) {
    multiMaterialQuad->set_position(glm::vec3(midX, botY, 0.0f));
    multiMaterialQuad->set_scale(glm::vec3(0.2f * objectScaleX, 0.2f * objectScaleY, 1.0f));
  }
  
  // Position multi-material cube with scale compensation
  if (multiMaterialCube) {
    multiMaterialCube->set_position(glm::vec3(rightX, botY, 0.0f));
    multiMaterialCube->set_scale(glm::vec3(0.2f * objectScaleX, 0.2f * objectScaleY, 0.2f));
  }
}
