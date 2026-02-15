#include "window.h"
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_events.h"
#include "SDL3/SDL_init.h"
#include "SDL3/SDL_version.h"
#include "SDL3/SDL_video.h"
#include "SDL3/SDL_vulkan.h"
#include "swapchain.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
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
      renderer_(std::move(other.renderer_)), shouldClose_(other.shouldClose_),
      minimized_(other.minimized_), focused_(other.focused_),
      fullscreen_(other.fullscreen_),
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
    shouldClose_ = other.shouldClose_;
    minimized_ = other.minimized_;
    focused_ = other.focused_;
    fullscreen_ = other.fullscreen_;
    eventCallback_ = std::move(other.eventCallback_);
  }
  return *this;
}

void Window::destroy() {
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
}

bool Window::create(const WindowConfig &config) {
  if (window_) {
    std::println(stderr, "[Window] Already created");
    return false;
  }

  mainGPU = config.mainGPU;
  secondaryGPUs = config.secondaryGPUs;

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
