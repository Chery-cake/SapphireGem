#include "window.h"
#include "renderer.h"
#include <memory>
#include <print>

Window::Window(int width, int heigth, std::string title) {
  glfwInit();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  window = glfwCreateWindow(width, heigth, title.c_str(), nullptr, nullptr);

  renderer = std::make_unique<Renderer>(window);
}

Window::~Window() {
  renderer.reset();

  glfwDestroyWindow(window);

  glfwTerminate();

  std::print("Window destructor executed\n");
}

void Window::run() {
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
  };
}
