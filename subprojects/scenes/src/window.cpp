#include "window.h"
#include "config.h"
#include "renderer.h"
#include "scene1.h"
#include "scene2.h"
#include "scene3.h"
#include <GLFW/glfw3.h>
#include <memory>
#include <print>
#include <stdexcept>

render::Window::Window(int width, int height, std::string title)
    : frameBufferResized(false), currentWidth(width), currentHeight(height),
      aspectRatio(static_cast<float>(width) / static_cast<float>(height)),
      currentSceneIndex(0) {
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
  renderer->set_post_reload_callback([this]() {
    std::print("Recreating active scene after reload...\n");
    create_scenes();
  });

  create_scenes();
}

render::Window::~Window() {
  scenes.clear();
  renderer.reset();

  glfwDestroyWindow(window);

  glfwTerminate();

  std::print("Window destructor executed\n");
}

void render::Window::frame_buffer_resize_callback(GLFWwindow *window, int width,
                                                  int height) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));
  win->frameBufferResized = true;
  win->currentWidth = width;
  win->currentHeight = height;
  win->aspectRatio = static_cast<float>(width) / static_cast<float>(height);
}

void render::Window::key_callback(GLFWwindow *window, int key, int scancode,
                                  int action, int mods) {
  auto *win = reinterpret_cast<Window *>(glfwGetWindowUserPointer(window));

  // Only trigger on key press, not release
  if (action != GLFW_PRESS) {
    return;
  }

  if (key == GLFW_KEY_R) {
    std::print("\n\nR key pressed - Reloading\n");
    win->renderer->reload();
  } else if (key == GLFW_KEY_F) {
    std::print("\n\nF key pressed - Full reload\n");
    general::Config::get_instance().set_reload(true);
    win->renderer->reload();
  } else if (key == GLFW_KEY_S) {
    std::print("\n\nS key pressed - Switching scene\n");
    win->switch_scene();
  }
}

void render::Window::create_scenes() {
  if (!renderer) {
    std::print("Error: Renderer not initialized\n");
    return;
  }

  // Clear existing scenes
  scenes.clear();

  // Get managers from renderer
  auto &materialMgr = renderer->get_material_manager();
  auto &textureMgr = renderer->get_texture_manager();
  auto &bufferMgr = renderer->get_buffer_manager();
  auto *objectMgr = renderer->get_object_manager();

  // Create the three scenes (but don't setup yet - lazy loading)
  std::print("Creating scene instances...\n");

  scenes.push_back(std::make_unique<scene::Scene1>(&materialMgr, &textureMgr,
                                                   &bufferMgr, objectMgr));
  scenes.push_back(std::make_unique<scene::Scene2>(&materialMgr, &textureMgr,
                                                   &bufferMgr, objectMgr));
  scenes.push_back(std::make_unique<scene::Scene3>(&materialMgr, &textureMgr,
                                                   &bufferMgr, objectMgr));

  // Setup only the first scene (active scene)
  if (!scenes.empty()) {
    std::print("Loading initial scene: {}\n",
               scenes[currentSceneIndex]->get_name());
    scenes[currentSceneIndex]->setup();
  }
}

void render::Window::switch_scene() {
  if (scenes.empty()) {
    return;
  }

  // Cleanup/unload current scene to free GPU resources
  std::print("Unloading scene: {}\n", scenes[currentSceneIndex]->get_name());
  scenes[currentSceneIndex]->cleanup();

  // Cycle to next scene
  size_t previousSceneIndex = currentSceneIndex;
  currentSceneIndex = (currentSceneIndex + 1) % scenes.size();

  // Setup the new scene (load into GPU)
  std::print("Switching to {}\n", scenes[currentSceneIndex]->get_name());
  std::print("Loading scene into GPU...\n");
  scenes[currentSceneIndex]->setup();
}

void render::Window::update_current_scene() {
  static double lastTime = glfwGetTime();
  double currentTime = glfwGetTime();
  float deltaTime = static_cast<float>(currentTime - lastTime);
  lastTime = currentTime;

  static float totalTime = 0.0f;
  totalTime += deltaTime;

  if (!scenes.empty() && currentSceneIndex < scenes.size()) {
    scenes[currentSceneIndex]->update(deltaTime, totalTime);
  }
}

void render::Window::run() {
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    if (frameBufferResized) {
      frameBufferResized = false;
      renderer->get_device_manager().recreate_swap_chain();
    }

    update_current_scene();

    renderer->draw_frame();
  };
}
