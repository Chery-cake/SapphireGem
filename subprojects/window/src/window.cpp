#include "window.h"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_version.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "scene.h"
#include "swapchain.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
#include <algorithm>
#include <memory>
#include <mutex>
#include <print>
#include <utility>

namespace window {

// ============================================================================
// Window Implementation
// ============================================================================

Window::Window() = default;

Window::~Window() { destroy(); }

Window::Window(Window &&other) noexcept
    : window_(std::exchange(other.window_, nullptr)),
      windowId_(std::exchange(other.windowId_, 0)), width_(other.width_),
      height_(other.height_), title_(std::move(other.title_)),
      mainGPU(std::exchange(other.mainGPU, nullptr)),
      secondaryGPUs(std::move(other.secondaryGPUs)),
      renderer_(std::move(other.renderer_)),
      allocator_(std::exchange(other.allocator_, nullptr)),
      shaderManager_(std::exchange(other.shaderManager_, nullptr)),
      scenes_(std::move(other.scenes_)),
      activeScenes_(std::move(other.activeScenes_)),
      shouldClose_(other.shouldClose_), minimized_(other.minimized_),
      focused_(other.focused_), fullscreen_(other.fullscreen_),
      eventCallback_(std::move(other.eventCallback_)) {}

Window &Window::operator=(Window &&other) noexcept {
  if (this != &other) {
    destroy();
    window_ = std::exchange(other.window_, nullptr);
    windowId_ = std::exchange(other.windowId_, 0);
    width_ = other.width_;
    height_ = other.height_;
    title_ = std::move(other.title_);
    mainGPU = std::exchange(other.mainGPU, nullptr);
    secondaryGPUs = std::move(other.secondaryGPUs);
    renderer_ = std::move(other.renderer_);
    allocator_ = std::exchange(other.allocator_, nullptr);
    shaderManager_ = std::exchange(other.shaderManager_, nullptr);
    scenes_ = std::move(other.scenes_);
    activeScenes_ = std::move(other.activeScenes_);
    shouldClose_ = other.shouldClose_;
    minimized_ = other.minimized_;
    focused_ = other.focused_;
    fullscreen_ = other.fullscreen_;
    eventCallback_ = std::move(other.eventCallback_);
  }
  return *this;
}

void Window::destroy() {
  // Wait for GPU to finish before unloading scenes
  if (renderer_) {
    renderer_->waitIdle();
  }

  // Unload all active scenes (they hold GPU resources)
  {
    std::lock_guard<std::mutex> lock(sceneMutex_);
    for (auto &[name, scene] : scenes_) {
      if (scene && scene->isLoaded()) {
        scene->unload();
      }
    }
    activeScenes_.clear();
    scenes_.clear();
  }

  if (renderer_) {
    renderer_.reset();
  }

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
    windowId_ = 0;
    std::println("[Window] Destroyed: {}", title_);
  }

  mainGPU = nullptr;
  secondaryGPUs.clear();
  allocator_ = nullptr;
  shaderManager_ = nullptr;
}

bool Window::create(const WindowConfig &config) {
  if (window_) {
    std::println(stderr, "[Window] Already created");
    return false;
  }

  mainGPU = config.mainGPU;
  secondaryGPUs = config.secondaryGPUs;
  allocator_ = config.allocator;
  shaderManager_ = config.shaderManager;

  // Build window flags
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN;
  if (config.fullscreen) {
    flags |= SDL_WINDOW_FULLSCREEN;
    fullscreen_ = true;
  }
  if (config.borderless) {
    flags |= SDL_WINDOW_BORDERLESS;
  }
  if (config.resizable) {
    flags |= SDL_WINDOW_RESIZABLE;
  }
  if (config.maximized) {
    flags |= SDL_WINDOW_MAXIMIZED;
  }
  if (config.highDPI) {
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  }

  // Determine position
  int x = (config.x < 0) ? SDL_WINDOWPOS_CENTERED : config.x;
  int y = (config.y < 0) ? SDL_WINDOWPOS_CENTERED : config.y;

  // Create window
  window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height,
                             flags);
  if (!window_) {
    std::println(stderr, "[Window] Failed to create window: {}",
                 SDL_GetError());
    return false;
  }

  // Set position if specified
  if (config.x >= 0 || config.y >= 0) {
    SDL_SetWindowPosition(window_, x, y);
  }

  windowId_ = SDL_GetWindowID(window_);
  title_ = config.title;

  // Get actual size (may differ due to high DPI)
  SDL_GetWindowSize(window_, &width_, &height_);

  std::println("[Window] Created: {} ({}x{})", title_, width_, height_);

  // Initialize rendering chain if Vulkan instance and allocator are provided
  if (config.vulkanInstance && config.mainGPU && config.allocator) {
    auto surface = createVulkanSurface(*config.vulkanInstance);
    if (surface == nullptr) {
      std::println(stderr, "[Window] Failed to create Vulkan surface for: {}",
                   title_);
      return false;
    }

    renderer_ = std::make_unique<Renderer>();
    if (!renderer_->initialize(*mainGPU, secondaryGPUs, surface,
                               config.swapchainConfig, *config.allocator,
                               static_cast<uint32_t>(width_),
                               static_cast<uint32_t>(height_))) {
      std::println(stderr, "[Window] Failed to initialize renderer for: {}",
                   title_);
      renderer_.reset();
      return false;
    }
    std::println("[Window] Renderer initialized for: {}", title_);
  }

  return true;
}

bool Window::pollEvents() {
  if (!window_) {
    return false;
  }

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    // Check if event is for this window
    if (event.type >= SDL_EVENT_WINDOW_FIRST &&
        event.type <= SDL_EVENT_WINDOW_LAST) {
      if (event.window.windowID != windowId_) {
        continue; // Not our event
      }

      WindowEvent windowEvent;

      switch (event.type) {
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        shouldClose_ = true;
        windowEvent.type = WindowEventType::Close;
        break;

      case SDL_EVENT_WINDOW_RESIZED:
        width_ = event.window.data1;
        height_ = event.window.data2;
        windowEvent.type = WindowEventType::Resize;
        windowEvent.width = width_;
        windowEvent.height = height_;
        break;

      case SDL_EVENT_WINDOW_MINIMIZED:
        minimized_ = true;
        windowEvent.type = WindowEventType::Minimize;
        break;

      case SDL_EVENT_WINDOW_MAXIMIZED:
        minimized_ = false;
        windowEvent.type = WindowEventType::Maximize;
        break;

      case SDL_EVENT_WINDOW_RESTORED:
        minimized_ = false;
        windowEvent.type = WindowEventType::Restore;
        break;

      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused_ = true;
        windowEvent.type = WindowEventType::Focus;
        break;

      case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused_ = false;
        windowEvent.type = WindowEventType::Unfocus;
        break;

      case SDL_EVENT_WINDOW_MOUSE_ENTER:
        windowEvent.type = WindowEventType::MouseEnter;
        break;

      case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        windowEvent.type = WindowEventType::MouseLeave;
        break;

      case SDL_EVENT_KEY_DOWN:
        windowEvent.type = WindowEventType::KeyDown;
        break;

      case SDL_EVENT_KEY_UP:
        windowEvent.type = WindowEventType::KeyUp;
        break;

      case SDL_EVENT_TEXT_INPUT:
        windowEvent.type = WindowEventType::TextInput;
        break;

      default:
        continue;
      }

      {
        std::lock_guard<std::mutex> lock(eventCallbackMutex_);
        auto it = eventCallback_.find(windowEvent.type);
        if (it != eventCallback_.end()) {
          for (const auto &func : it->second) {
            func(windowEvent);
          }
        }
      }
    }

    // Handle quit event
    if (event.type == SDL_EVENT_QUIT) {
      shouldClose_ = true;
      std::lock_guard<std::mutex> lock(eventCallbackMutex_);
      auto it = eventCallback_.find(WindowEventType::Close);
      if (it != eventCallback_.end()) {
        WindowEvent quitEvent;
        quitEvent.type = WindowEventType::Close;
        for (const auto &func : it->second) {
          func(quitEvent);
        }
      }
    }
  }

  return true;
}

void Window::setEventCallback(WindowEventCallback callback,
                              WindowEventType eventType) {
  std::lock_guard<std::mutex> lock(eventCallbackMutex_);
  eventCallback_[eventType].push_back(std::move(callback));
}

vk::raii::SurfaceKHR
Window::createVulkanSurface(const vk::raii::Instance &instance) {
  if (!window_) {
    std::println(stderr, "[Window] Cannot create surface: window not created");
    return nullptr;
  }

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(*instance),
                                nullptr, &surface)) {
    std::println(stderr, "[Window] Failed to create Vulkan surface: {}",
                 SDL_GetError());
    return nullptr;
  }

  return vk::raii::SurfaceKHR(instance, surface);
}

std::vector<std::string> Window::getRequiredVulkanExtensions() {
  std::vector<std::string> extensions;

  uint32_t count = 0;
  const char *const *sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
  if (sdlExtensions) {
    extensions.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      extensions.emplace_back(sdlExtensions[i]);
    }
  }

  return extensions;
}

void Window::setTitle(const std::string &title) {
  if (window_) {
    SDL_SetWindowTitle(window_, title.c_str());
    title_ = title;
  }
}

void Window::resize(int32_t width, int32_t height) {
  if (window_) {
    SDL_SetWindowSize(window_, width, height);
    width_ = width;
    height_ = height;
  }
}

void Window::setFullscreen(bool fullscreen) {
  if (window_) {
    SDL_SetWindowFullscreen(window_, fullscreen);
    fullscreen_ = fullscreen;
  }
}

void Window::setMainGPU(device::GPUDevice *device) {
  std::lock_guard<std::mutex> lock(windowMutex_);
  mainGPU = device;
  if (renderer_ && renderer_->getSwapchain()) {
    renderer_->getSwapchain()->updateMainDevice(device);
  }
}

void Window::setSecondaryGPUs(std::vector<device::GPUDevice *> &devices) {
  std::lock_guard<std::mutex> lock(windowMutex_);
  secondaryGPUs = devices;
  if (renderer_ && renderer_->getSwapchain()) {
    renderer_->getSwapchain()->updateSecondaryDevices(devices);
  }
}

// ============================================================================
// Scene Management Implementation
// ============================================================================

void Window::addScene(std::shared_ptr<Scene> scene) {
  if (!scene) {
    std::println(stderr, "[Window] Cannot add null scene");
    return;
  }
  std::lock_guard<std::mutex> lock(sceneMutex_);
  std::println("[Window] Added scene: {}", scene->getName());
  scenes_[scene->getName()] = std::move(scene);
}

void Window::removeScene(const std::string &name) {
  std::shared_ptr<Scene> sceneToUnload;

  {
    std::lock_guard<std::mutex> lock(sceneMutex_);

    // Remove from active list
    auto activeIt =
        std::find(activeScenes_.begin(), activeScenes_.end(), name);
    if (activeIt != activeScenes_.end()) {
      activeScenes_.erase(activeIt);
    }

    auto sceneIt = scenes_.find(name);
    if (sceneIt != scenes_.end()) {
      if (sceneIt->second && sceneIt->second->isLoaded()) {
        sceneToUnload = sceneIt->second;
      }
      scenes_.erase(sceneIt);
    }
  }

  // Unload outside the lock to avoid holding sceneMutex_ during GPU wait
  if (sceneToUnload) {
    if (renderer_) {
      renderer_->waitIdle();
    }
    sceneToUnload->unload();
    std::println("[Window] Removed and unloaded scene: {}", name);
  } else {
    std::println("[Window] Removed scene: {}", name);
  }
}

bool Window::presentScene(const std::string &name) {
  std::lock_guard<std::mutex> lock(sceneMutex_);

  auto it = scenes_.find(name);
  if (it == scenes_.end()) {
    std::println(stderr, "[Window] Scene not found: {}", name);
    return false;
  }

  // Check if already active
  for (const auto &active : activeScenes_) {
    if (active == name) {
      return true;
    }
  }

  // Load if not loaded
  if (!it->second->isLoaded()) {
    if (!mainGPU || !allocator_ || !shaderManager_ || !renderer_) {
      std::println(
          stderr,
          "[Window] Cannot load scene '{}': missing GPU, allocator, "
          "shader manager, or renderer",
          name);
      return false;
    }

    if (!it->second->load(*mainGPU, secondaryGPUs, *allocator_,
                          *shaderManager_, *renderer_)) {
      std::println(stderr, "[Window] Failed to load scene: {}", name);
      return false;
    }
  }

  activeScenes_.push_back(name);
  std::println("[Window] Presenting scene: {}", name);
  return true;
}

void Window::unpresentScene(const std::string &name) {
  std::shared_ptr<Scene> sceneToUnload;

  {
    std::lock_guard<std::mutex> lock(sceneMutex_);

    // Remove from active list
    auto it = std::find(activeScenes_.begin(), activeScenes_.end(), name);
    if (it != activeScenes_.end()) {
      activeScenes_.erase(it);
    }

    auto sceneIt = scenes_.find(name);
    if (sceneIt != scenes_.end() && sceneIt->second->isLoaded()) {
      sceneToUnload = sceneIt->second;
    }
  }

  // Unload outside the lock to avoid holding sceneMutex_ during GPU wait
  if (sceneToUnload) {
    if (renderer_) {
      renderer_->waitIdle();
    }
    sceneToUnload->unload();
    std::println("[Window] Unpresented and unloaded scene: {}", name);
  }
}

bool Window::renderFrame(float deltaTime) {
  if (!renderer_ || !renderer_->isInitialized()) {
    return false;
  }
  if (isMinimized()) {
    return true; // Skip rendering when minimized
  }

  // Collect active loaded scenes under lock (shared_ptrs keep them alive)
  std::vector<std::shared_ptr<Scene>> scenesToRender;
  {
    std::lock_guard<std::mutex> lock(sceneMutex_);
    for (const auto &name : activeScenes_) {
      auto it = scenes_.find(name);
      if (it != scenes_.end() && it->second->isLoaded()) {
        scenesToRender.push_back(it->second);
      }
    }
  }

  if (scenesToRender.empty()) {
    return true;
  }

  // Update scenes outside the lock
  for (auto &scene : scenesToRender) {
    scene->update(deltaTime);
  }

  // Begin frame
  auto *sync = renderer_->beginFrame();
  if (!sync) {
    return false;
  }

  uint32_t imageIndex = renderer_->getCurrentImageIndex();
  uint32_t frameIndex = renderer_->getCurrentFrame();
  vk::CommandBuffer cmd = sync->commandBuffer;

  // Verify swapchain and render pass are valid before rendering
  auto *swapchain = renderer_->getSwapchain();
  if (!swapchain || !swapchain->isValid()) {
    std::println(stderr, "[Window] Invalid swapchain for rendering");
    return false;
  }

  vk::RenderPass renderPass = renderer_->getRenderPass();
  if (!renderPass) {
    std::println(stderr, "[Window] Invalid render pass for rendering");
    return false;
  }

  // Begin render pass
  auto clearValues = renderer_->getClearValues();

  vk::RenderPassBeginInfo renderPassInfo{
      renderPass, **swapchain->getFrame(imageIndex).framebuffer,
      vk::Rect2D{{0, 0}, swapchain->getExtent()}, clearValues};

  cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

  // Set dynamic viewport and scissor
  vk::Viewport viewport{0.0f,
                         0.0f,
                         static_cast<float>(swapchain->getExtent().width),
                         static_cast<float>(swapchain->getExtent().height),
                         0.0f,
                         1.0f};
  cmd.setViewport(0, viewport);

  vk::Rect2D scissor{{0, 0}, swapchain->getExtent()};
  cmd.setScissor(0, scissor);

  // Draw all active scenes
  for (auto &scene : scenesToRender) {
    scene->draw(cmd, frameIndex);
  }

  cmd.endRenderPass();

  // End frame and present
  return renderer_->endFrame(*sync, imageIndex);
}

Scene *Window::getScene(const std::string &name) {
  std::lock_guard<std::mutex> lock(sceneMutex_);
  auto it = scenes_.find(name);
  if (it != scenes_.end()) {
    return it->second.get();
  }
  return nullptr;
}

std::vector<std::string> Window::getActiveSceneNames() const {
  std::lock_guard<std::mutex> lock(sceneMutex_);
  return activeScenes_;
}

// ============================================================================
// WindowManager Implementation
// ============================================================================

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() { shutdown(); }

bool WindowManager::initialize() {
  if (initialized_) {
    std::println(stderr, "[WindowManager] Already initialized");
    return false;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
    std::println(stderr, "[WindowManager] Failed to initialize SDL: {}",
                 SDL_GetError());
    return false;
  }

  initialized_ = true;
  std::println("[WindowManager] Initialized (SDL {})", SDL_GetVersion());
  return true;
}

void WindowManager::shutdown() {
  if (!initialized_) {
    return;
  }

  // Destroy all windows
  windows_.clear();

  SDL_Quit();
  initialized_ = false;
  std::println("[WindowManager] Shutdown complete");
}

Window *WindowManager::createWindow(const WindowConfig &config) {
  if (!initialized_) {
    std::println(stderr, "[WindowManager] Not initialized");
    return nullptr;
  }

  auto window = std::make_unique<Window>();
  if (!window->create(config)) {
    return nullptr;
  }

  Window *ptr = window.get();
  windows_.push_back(std::move(window));
  return ptr;
}

void WindowManager::destroyWindow(Window *window) {
  auto it = std::find_if(
      windows_.begin(), windows_.end(),
      [window](const std::unique_ptr<Window> &w) { return w.get() == window; });

  if (it != windows_.end()) {
    windows_.erase(it);
  }
}

Window *WindowManager::getWindowById(uint32_t windowId) {
  for (auto &window : windows_) {
    if (window->getWindowId() == windowId) {
      return window.get();
    }
  }
  return nullptr;
}

Window *WindowManager::getPrimaryWindow() {
  if (windows_.empty()) {
    return nullptr;
  }
  return windows_.front().get();
}

void WindowManager::pollAllEvents() {
  // Poll sequentially
  for (auto &window : windows_) {
    window->pollEvents();
  }
}

bool WindowManager::anyWindowShouldClose() const {
  for (const auto &window : windows_) {
    if (window->shouldClose()) {
      return true;
    }
  }
  return false;
}

} // namespace window
