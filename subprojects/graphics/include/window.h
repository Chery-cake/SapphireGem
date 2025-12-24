#pragma once

#include "renderer.h"
#include "scene.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <string>

namespace render {

class Window {

private:
  GLFWwindow *window;

  bool frameBufferResized;

  std::unique_ptr<Renderer> renderer;

  // Window dimensions for aspect ratio calculations
  int currentWidth;
  int currentHeight;
  float aspectRatio;

  // Scene management
  std::vector<std::unique_ptr<Scene>> scenes;
  size_t currentSceneIndex;

  static void frame_buffer_resize_callback(GLFWwindow *window, int width,
                                           int height);
  static void key_callback(GLFWwindow *window, int key, int scancode,
                           int action, int mods);

  void create_scenes();
  void switch_scene();
  void update_current_scene();

public:
  Window(int width, int height, std::string title);
  ~Window();

  void run();
};

} // namespace render
