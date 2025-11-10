#pragma once

#include "renderer.h"
#include <memory>
#include <string>

class Window {

private:
  GLFWwindow *window;

  std::unique_ptr<Renderer> renderer;

public:
  Window(int width, int heigth, std::string title);
  ~Window();

  void run();
};
