#include "window.h"
#include "renderer.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <print>
#include <stdexcept>

Window::Window(int width, int height, std::string title)
    : frameBufferRezized(false), triangle(nullptr), cube(nullptr) {
  glfwInit();

  if (!glfwVulkanSupported()) {
    throw std::runtime_error("GLFW can't load Vulkan\n");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  glfwSetWindowUserPointer(window, this);
  glfwSetFramebufferSizeCallback(window, frame_buffer_resize_callback);

  renderer = std::make_unique<Renderer>(window);

  create_scene_objects();
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

  // Create a 2D triangle on the left side
  triangle = renderer->create_triangle_2d(
      "triangle", glm::vec3(-0.5f, 0.0f, 0.0f), // Position (left)
      glm::vec3(0.0f, 0.0f, 0.0f),              // Rotation
      glm::vec3(0.5f, 0.5f, 1.0f));             // Scale

  // Create a 3D cube on the right side
  cube = renderer->create_cube_3d(
      "cube", glm::vec3(0.5f, 0.0f, 0.0f), // Position (right)
      glm::vec3(0.0f, 0.0f, 0.0f),         // Rotation
      glm::vec3(0.3f, 0.3f, 1.0f));        // Scale

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
}
