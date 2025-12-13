#pragma once

#include "renderer.h"
#include "render_object.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

class Window {

private:
  GLFWwindow *window;

  bool frameBufferRezized;

  std::unique_ptr<Renderer> renderer;

  // Objects for animation
  RenderObject *triangle;
  RenderObject *cube;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);
  
  void create_scene_objects();
  void update_scene_objects();

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};
