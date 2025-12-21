#pragma once

#include "object.h"
#include "renderer.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

namespace render {

class Window {

private:
  GLFWwindow *window;

  bool frameBufferRezized;

  std::unique_ptr<Renderer> renderer;

  // Objects for animation
  Object *triangle;
  Object *cube;
  Object *texturedSquare;
  Object *imageQuad;
  
  // New objects for requirements
  Object *atlasQuad1;
  Object *atlasQuad2;
  Object *atlasQuad3;
  Object *atlasQuad4;
  Object *multiMaterialQuad;
  Object *multiMaterialCube;
  
  // Window dimensions for aspect ratio calculations
  int currentWidth;
  int currentHeight;
  float aspectRatio;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);
  static void key_callback(GLFWwindow *window, int key, int scancode,
                           int action, int mods);

  void create_scene_objects();
  void update_scene_objects();
  void update_object_positions();

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};

} // namespace render
