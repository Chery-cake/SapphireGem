#include "window.h"
#include "object.h"
#include "renderer.h"
#include "texture.h"
#include "texture_manager.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <print>
#include <stdexcept>

render::Window::Window(int width, int height, std::string title)
    : frameBufferRezized(false), triangle(nullptr), cube(nullptr),
      texturedSquare(nullptr), imageQuad(nullptr) {
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
  std::print("Loading textures...\n");

  // Load first texture (checkerboard) for the square
  auto *checkerboardTex = textureMgr.create_texture(
      "checkerboard", "../assets/textures/checkerboard.png");
  if (checkerboardTex) {
    std::print("Loaded checkerboard texture: {}x{}\n",
               checkerboardTex->get_width(), checkerboardTex->get_height());

    // Apply a green tint to demonstrate color manipulation
    checkerboardTex->set_color_tint(glm::vec4(0.7f, 1.0f, 0.7f, 1.0f));
    checkerboardTex->update_gpu();
    std::print("Applied green tint to checkerboard texture\n");
  }

  // Load second texture (gradient) for the plain image
  auto *gradientTex =
      textureMgr.create_texture("gradient", "../assets/textures/gradient.png");
  if (gradientTex) {
    std::print("Loaded gradient texture: {}x{}\n", gradientTex->get_width(),
               gradientTex->get_height());

    // Rotate the gradient image to demonstrate rotation
    gradientTex->rotate_90_clockwise();
    gradientTex->update_gpu();
    std::print("Rotated gradient texture 90 degrees clockwise\n");
  }

  // Create separate materials for each textured object
  auto &materialMgr = renderer->get_material_manager();
  auto &bufferMgr = renderer->get_buffer_manager();

  struct TransformUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };

  TransformUBO uboData = {
      .model = glm::mat4(1.0f), // Identity matrix
      .view = glm::mat4(1.0f),  // Identity matrix
      .proj = glm::mat4(1.0f)   // Identity matrix for 2D (using NDC directly)
  };

  // Create material bindings for textured objects
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
      Material::Vertex2DTextured::getBindingDescription();
  auto texturedAttributeDescriptions =
      Material::Vertex2DTextured::getAttributeDescriptions();

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  // Create material for checkerboard texture
  {
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
        .size = sizeof(TransformUBO),
        .elementSize = sizeof(TransformUBO),
        .initialData = &uboData};

    bufferMgr.create_buffer(uboInfo);
  }

  // Create material for gradient texture
  {
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
        .size = sizeof(TransformUBO),
        .elementSize = sizeof(TransformUBO),
        .initialData = &uboData};

    bufferMgr.create_buffer(uboInfo);
  }

  // Create the objects first (they will look for their specific materials)
  std::print("Creating textured square object...\n");

  texturedSquare = renderer->create_textured_square_2d(
      "textured_square", "checkerboard",
      glm::vec3(-0.5f, 0.5f, 0.0f), // Position (top-left)
      glm::vec3(0.0f, 0.0f, 0.0f),  // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f)); // Scale

  std::print("Creating image quad object...\n");
  imageQuad = renderer->create_textured_square_2d(
      "image_quad", "gradient",
      glm::vec3(0.5f, 0.5f, 0.0f),  // Position (top-right)
      glm::vec3(0.0f, 0.0f, 0.0f),  // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f)); // Scale

  // Bind textures to their respective materials
  auto *checkerboardMaterial =
      materialMgr.get_material("Textured_checkerboard");
  if (checkerboardMaterial && checkerboardTex) {
    auto *uboBuffer = bufferMgr.get_buffer("Textured_checkerboard_ubo");
    if (uboBuffer) {
      checkerboardMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
    }
    checkerboardMaterial->bind_texture(checkerboardTex->get_image().get(), 1,
                                       0);
    std::print(
        "✓ Bound checkerboard texture to Textured_checkerboard material\n");
  }

  auto *gradientMaterial = materialMgr.get_material("Textured_gradient");
  if (gradientMaterial && gradientTex) {
    auto *uboBuffer = bufferMgr.get_buffer("Textured_gradient_ubo");
    if (uboBuffer) {
      gradientMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
    }
    gradientMaterial->bind_texture(gradientTex->get_image().get(), 1, 0);
    std::print("✓ Bound gradient texture to Textured_gradient material\n");
  }

  // Create a 2D triangle on the left side
  triangle = renderer->create_triangle_2d(
      "triangle", glm::vec3(-0.5f, -0.5f, 0.0f), // Position (left)
      glm::vec3(0.0f, 0.0f, 0.0f),               // Rotation
      glm::vec3(0.5f, 0.5f, 1.0f));              // Scale
  
  // Set triangle to use 2D transform rotation (X and Y axes)
  if (triangle) {
    triangle->set_rotation_mode(Object::RotationMode::TRANSFORM_2D);
  }

  // Create a 3D cube on the right side
  cube = renderer->create_cube_3d(
      "cube", glm::vec3(0.5f, -0.5f, 0.0f), // Position (right)
      glm::vec3(0.0f, 0.0f, 0.0f),          // Rotation
      glm::vec3(0.3f, 0.3f, 0.3f));         // Scale (make it slightly smaller for better visibility)
  
  // Ensure cube uses 3D transform rotation (all axes) - this is the default for 3D objects
  if (cube) {
    cube->set_rotation_mode(Object::RotationMode::TRANSFORM_3D);
  }

  std::print("Scene objects created: triangle and cube\n");
}

void render::Window::update_scene_objects() {
  static double lastTime = glfwGetTime();
  double currentTime = glfwGetTime();
  float deltaTime = static_cast<float>(currentTime - lastTime);
  lastTime = currentTime;

  static float time = 0.0f;
  time += deltaTime;

  if (triangle) {
    // Rotate triangle in X and Y axes simultaneously (2D rotation mode)
    triangle->rotate(glm::vec3(time * 0.5f, time * 0.7f, 0.0f));
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
}
