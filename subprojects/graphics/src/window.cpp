#include "window.h"
#include "render_object.h"
#include "renderer.h"
#include "texture.h"
#include "texture_manager.h"
#include <GLFW/glfw3.h>
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

  // Create a square object (will represent "textured square" visually)
  texturedSquare = renderer->create_square_2d(
      "textured_square", glm::vec3(-0.5f, 0.5f, 0.0f), // Position (top-left)
      glm::vec3(0.0f, 0.0f, 0.0f),                     // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f));                    // Scale

  // Create another square for the "plain image"
  imageQuad = renderer->create_square_2d(
      "image_quad", glm::vec3(0.5f, 0.5f, 0.0f), // Position (top-right)
      glm::vec3(0.0f, 0.0f, 0.0f),               // Rotation
      glm::vec3(0.4f, 0.4f, 1.0f));              // Scale

  std::print("Scene objects created: textured square and image quad\n");
  std::print(
      "Note: Textures are loaded and manipulated, but shader support for \n");
  std::print(
      "texture sampling would be needed to display them on these objects.\n");
  std::print("The squares are shown with vertex colors representing where \n");
  std::print("textures would be applied in a full implementation.\n");

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
