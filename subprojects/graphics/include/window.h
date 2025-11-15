#pragma once

#include "renderer.h"
#include <memory>
#include <string>

class Window {

private:
  GLFWwindow *window;

  bool frameBufferRezized;

  std::unique_ptr<Renderer> renderer;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};
