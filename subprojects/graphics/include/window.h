#pragma once

#include "render_object.h"
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
  SapphireGem::Graphics::Object *triangle;
  SapphireGem::Graphics::Object *cube;
  SapphireGem::Graphics::Object *texturedSquare;
  SapphireGem::Graphics::Object *imageQuad;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);
  static void key_callback(GLFWwindow *window, int key, int scancode,
                           int action, int mods);

  void create_scene_objects();
  void update_scene_objects();

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};
