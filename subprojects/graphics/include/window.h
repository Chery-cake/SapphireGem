#pragma once

#include "renderer.h"
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

  // Current transform mode
  RenderObject::TransformMode currentTransformMode;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);
  static void key_callback(GLFWwindow *window, int key, int scancode,
                          int action, int mods);

  void create_scene_objects();
  void update_scene_objects();
  void toggle_transform_mode();

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};
