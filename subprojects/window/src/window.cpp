#include "window.h"
#include "thread_manager.h"
#include <iostream>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace window {

// ============================================================================
// Window Implementation
// ============================================================================

Window::Window() = default;

Window::~Window() {
    destroy();
}

Window::Window(Window&& other) noexcept
    : window_(other.window_)
    , windowId_(other.windowId_)
    , width_(other.width_)
    , height_(other.height_)
    , title_(std::move(other.title_))
    , shouldClose_(other.shouldClose_)
    , minimized_(other.minimized_)
    , focused_(other.focused_)
    , fullscreen_(other.fullscreen_)
    , eventCallback_(std::move(other.eventCallback_)) {
    other.window_ = nullptr;
    other.windowId_ = 0;
}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        window_ = other.window_;
        windowId_ = other.windowId_;
        width_ = other.width_;
        height_ = other.height_;
        title_ = std::move(other.title_);
        shouldClose_ = other.shouldClose_;
        minimized_ = other.minimized_;
        focused_ = other.focused_;
        fullscreen_ = other.fullscreen_;
        eventCallback_ = std::move(other.eventCallback_);
        other.window_ = nullptr;
        other.windowId_ = 0;
    }
    return *this;
}

bool Window::create(const WindowConfig& config) {
    if (window_) {
        std::cerr << "[Window] Already created" << std::endl;
        return false;
    }

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
    window_ = SDL_CreateWindow(config.title.c_str(), config.width, config.height, flags);
    if (!window_) {
        std::cerr << "[Window] Failed to create window: " << SDL_GetError() << std::endl;
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

    std::cout << "[Window] Created: " << title_ << " (" << width_ << "x" << height_ << ")" << std::endl;
    return true;
}

void Window::destroy() {
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        windowId_ = 0;
        std::cout << "[Window] Destroyed: " << title_ << std::endl;
    }
}

bool Window::pollEvents() {
    if (!window_) {
        return false;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Check if event is for this window
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            if (event.window.windowID != windowId_) {
                continue;  // Not our event
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

                default:
                    continue;
            }

            if (eventCallback_ && windowEvent.type != WindowEventType::None) {
                eventCallback_(windowEvent);
            }
        }

        // Handle quit event
        if (event.type == SDL_EVENT_QUIT) {
            shouldClose_ = true;
            if (eventCallback_) {
                WindowEvent quitEvent;
                quitEvent.type = WindowEventType::Close;
                eventCallback_(quitEvent);
            }
        }
    }

    return true;
}

void Window::setEventCallback(WindowEventCallback callback) {
    eventCallback_ = std::move(callback);
}

vk::SurfaceKHR Window::createVulkanSurface(vk::Instance instance) {
    if (!window_) {
        std::cerr << "[Window] Cannot create surface: window not created" << std::endl;
        return nullptr;
    }

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, static_cast<VkInstance>(instance), nullptr, &surface)) {
        std::cerr << "[Window] Failed to create Vulkan surface: " << SDL_GetError() << std::endl;
        return nullptr;
    }

    return vk::SurfaceKHR(surface);
}

std::vector<std::string> Window::getRequiredVulkanExtensions() {
    std::vector<std::string> extensions;

    uint32_t count = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (sdlExtensions) {
        extensions.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            extensions.emplace_back(sdlExtensions[i]);
        }
    }

    return extensions;
}

void Window::setTitle(const std::string& title) {
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

// ============================================================================
// WindowManager Implementation
// ============================================================================

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() {
    shutdown();
}

bool WindowManager::initialize() {
    if (initialized_) {
        std::cerr << "[WindowManager] Already initialized" << std::endl;
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::cerr << "[WindowManager] Failed to initialize SDL: " << SDL_GetError() << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "[WindowManager] Initialized (SDL " << SDL_GetVersion() << ")" << std::endl;
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
    std::cout << "[WindowManager] Shutdown complete" << std::endl;
}

Window* WindowManager::createWindow(const WindowConfig& config) {
    if (!initialized_) {
        std::cerr << "[WindowManager] Not initialized" << std::endl;
        return nullptr;
    }

    auto window = std::make_unique<Window>();
    if (!window->create(config)) {
        return nullptr;
    }

    Window* ptr = window.get();
    windows_.push_back(std::move(window));
    return ptr;
}

void WindowManager::destroyWindow(Window* window) {
    auto it = std::find_if(windows_.begin(), windows_.end(),
        [window](const std::unique_ptr<Window>& w) { return w.get() == window; });

    if (it != windows_.end()) {
        windows_.erase(it);
    }
}

Window* WindowManager::getWindowById(uint32_t windowId) {
    for (auto& window : windows_) {
        if (window->getWindowId() == windowId) {
            return window.get();
        }
    }
    return nullptr;
}

Window* WindowManager::getPrimaryWindow() {
    if (windows_.empty()) {
        return nullptr;
    }
    return windows_.front().get();
}

void WindowManager::pollAllEvents() {
    // Use main loop thread pool if available
    if (windows_.size() > 1 && core::ThreadManager::instance().hasPool("mainloop")) {
        std::vector<std::future<void>> futures;
        futures.reserve(windows_.size());

        for (auto& window : windows_) {
            futures.push_back(
                core::ThreadManager::instance().submitTo("mainloop", [&window]() {
                    window->pollEvents();
                })
            );
        }

        for (auto& future : futures) {
            future.wait();
        }
    } else {
        // Poll sequentially
        for (auto& window : windows_) {
            window->pollEvents();
        }
    }
}

bool WindowManager::anyWindowShouldClose() const {
    for (const auto& window : windows_) {
        if (window->shouldClose()) {
            return true;
        }
    }
    return false;
}

} // namespace window
