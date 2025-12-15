#include "window.h"
#include "buffer.h"
#include "render_object.h"
#include "renderer.h"
#include "texture.h"
#include "texture_manager.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <memory>
#include <print>
#include <stdexcept>

Window::Window(int width, int height, std::string title)
    : frameBufferRezized(false),
      currentTransformMode(RenderObject::TransformMode::CPU_VERTICES),
      triangle(nullptr), cube(nullptr), texturedSquare(nullptr),
      imageQuad(nullptr) {
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
  std::print("Press 'T' to toggle between CPU (vertex) and GPU (matrix) "
             "transformation modes\n");
}

Window::~Window() {
  renderer.reset();

  glfwDestroyWindow(window);

  glfwTerminate();

  std::print("Window destructor executed\n");
}

void Window::frame_buffer_resize_callback(GLFWwindow *window, int width,
                                          int height) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));
  win->frameBufferRezized = true;
}

void Window::key_callback(GLFWwindow *window, int key, int scancode, int action,
                          int mods) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));

  if (action == GLFW_PRESS) {
    if (key == GLFW_KEY_T) {
      win->toggle_transform_mode();
    }
  }
}

void Window::run() {
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

void Window::create_scene_objects() {
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

  // Create and bind uniform buffer for Textured material
  struct TransformUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
  };
  
  TransformUBO uboData = {
    .model = glm::mat4(1.0f),  // Identity matrix
    .view = glm::mat4(1.0f),   // Identity matrix
    .proj = glm::mat4(1.0f)    // Identity matrix for 2D (using NDC directly)
  };
  
  Buffer::BufferCreateInfo uboInfo = {
    .identifier = "textured_ubo",
    .type = Buffer::BufferType::UNIFORM,
    .usage = Buffer::BufferUsage::DYNAMIC,
    .size = sizeof(TransformUBO),
    .elementSize = sizeof(TransformUBO),
    .initialData = &uboData
  };
  
  auto &bufferMgr = renderer->get_buffer_manager();
  bufferMgr.create_buffer(uboInfo);
  auto *uboBuffer = bufferMgr.get_buffer("textured_ubo");
  
  // Bind textures and UBO to the Textured material
  auto &materialMgr = renderer->get_material_manager();
  auto *texturedMaterial = materialMgr.get_material("Textured");
  
  if (texturedMaterial && uboBuffer) {
    texturedMaterial->bind_uniform_buffer(uboBuffer, 0, 0);
    std::print("Bound uniform buffer to Textured material\n");
  }
  
  if (texturedMaterial && checkerboardTex) {
    texturedMaterial->bind_texture(checkerboardTex->get_image().get(), 1, 0);
    std::print("Bound checkerboard texture to Textured material\n");
  }

  // Create a textured square object with checkerboard texture
  std::print("Creating textured square object...\n");
  texturedSquare = renderer->create_textured_square_2d(
      "textured_square", "checkerboard",
      glm::vec3(-0.5f, 0.5f, 0.0f), // Position (top-left)
      glm::vec3(0.0f, 0.0f, 0.0f),  // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f)); // Scale

  if (texturedSquare) {
    std::print("✓ Textured square created successfully\n");
  } else {
    std::print("✗ Failed to create textured square\n");
  }

  // For the second quad, we'll need a different approach since we can only bind one texture
  // to the material at a time. For now, let's create it but it will show the checkerboard too.
  // In a production system, you'd want per-object texture binding.
  std::print("Creating image quad object...\n");
  imageQuad = renderer->create_textured_square_2d(
      "image_quad", "gradient",
      glm::vec3(0.5f, 0.5f, 0.0f), // Position (top-right)
      glm::vec3(0.0f, 0.0f, 0.0f), // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f)); // Scale

  if (imageQuad) {
    std::print("✓ Image quad created successfully\n");
  } else {
    std::print("✗ Failed to create image quad\n");
  }
  
  // Bind gradient texture - this will be shown on both quads since they share the material
  // This is a limitation of having a single shared material for multiple textured objects
  if (texturedMaterial && gradientTex) {
    texturedMaterial->bind_texture(gradientTex->get_image().get(), 1, 0);
    std::print("Bound gradient texture to Textured material - both quads will show this texture\n");
  }

  // Create a 2D triangle on the left side
  triangle = renderer->create_triangle_2d(
      "triangle", glm::vec3(-0.5f, -0.5f, 0.0f), // Position (left)
      glm::vec3(0.0f, 0.0f, 0.0f),               // Rotation
      glm::vec3(0.5f, 0.5f, 1.0f));              // Scale

  // Create a 3D cube on the right side
  cube = renderer->create_cube_3d(
      "cube", glm::vec3(0.5f, -0.5f, 0.0f), // Position (right)
      glm::vec3(0.0f, 0.0f, 0.0f),          // Rotation
      glm::vec3(0.3f, 0.3f, 1.0f));         // Scale

  std::print("Scene objects created: triangle and cube\n");
}

void Window::update_scene_objects() {
  static double lastTime = glfwGetTime();
  double currentTime = glfwGetTime();
  float deltaTime = static_cast<float>(currentTime - lastTime);
  lastTime = currentTime;

  static float time = 0.0f;
  time += deltaTime;

  if (triangle) {
    // Rotate triangle around Z axis
    triangle->set_rotation(glm::vec3(0.0f, 0.0f, time));
  }

  if (cube) {
    // Rotate cube around Z axis (faster than triangle)
    cube->set_rotation(glm::vec3(0.0f, 0.0f, time * 1.5f));
  }

  if (texturedSquare) {
    // Gentle rotation for textured square
    texturedSquare->set_rotation(glm::vec3(0.0f, 0.0f, time * 0.5f));
  }

  if (imageQuad) {
    // Slightly faster rotation for image quad
    imageQuad->set_rotation(glm::vec3(0.0f, 0.0f, time * 0.8f));
  }
}

void Window::toggle_transform_mode() {
  // Toggle between CPU and GPU modes
  if (currentTransformMode == RenderObject::TransformMode::CPU_VERTICES) {
    currentTransformMode = RenderObject::TransformMode::GPU_MATRIX;
  } else {
    currentTransformMode = RenderObject::TransformMode::CPU_VERTICES;
  }

  // Apply to all objects
  if (triangle) {
    triangle->set_transform_mode(currentTransformMode);
  }
  if (cube) {
    cube->set_transform_mode(currentTransformMode);
  }
  if (texturedSquare) {
    texturedSquare->set_transform_mode(currentTransformMode);
  }
  if (imageQuad) {
    imageQuad->set_transform_mode(currentTransformMode);
  }

  std::print("Transform mode switched to: {}\n",
             currentTransformMode == RenderObject::TransformMode::CPU_VERTICES
                 ? "CPU (vertex transformation)"
                 : "GPU (matrix transformation)");
}
