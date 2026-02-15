#ifndef WINDOW_H_
#define WINDOW_H_

#include "swapchain.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
#include "window_export.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare SDL types to avoid including SDL.h in header
struct SDL_Window;

// Forward declare device types
namespace device {
class GPUDevice;
}

namespace window {

// Forward declare swapchain config
struct SwapchainConfig;

/**
 * @brief Window creation configuration
 */
struct WINDOW_API WindowConfig {
  std::string title = "SapphireEngine";
  int32_t width = 1280;
  int32_t height = 720;
  int32_t x = -1; // -1 = centered
  int32_t y = -1; // -1 = centered
  bool fullscreen = false;
  bool borderless = false;
  bool resizable = true;
  bool maximized = false;
  bool vsync = true;
  bool highDPI = true;
  device::GPUDevice *mainGPU;
  std::vector<device::GPUDevice *> secondaryGPUs = {};
};

/**
 * @brief Window event types
 */
enum class WindowEventType : uint8_t {
  None,
  Close,
  Resize,
  Minimize,
  Maximize,
  Restore,
  Focus,
  Unfocus,
  MouseEnter,
  MouseLeave,
  KeyDown,
  KeyUp,
  TextInput
};

/**
 * @brief Window event data
 */
struct WINDOW_API WindowEvent {
  WindowEventType type = WindowEventType::None;
  int32_t width = 0;
  int32_t height = 0;
};

/**
 * @brief Callback for window events
 */
using WindowEventCallback = std::function<void(const WindowEvent &)>;

/**
 * @brief Manages an SDL3 window with Vulkan surface support
 *
 * Thread-safe window management with event polling.
 */
class WINDOW_API Window {
public:
  Window();
  ~Window();

  // Disable copy
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;

  // Enable move
  Window(Window &&other) noexcept;
  Window &operator=(Window &&other) noexcept;

  /**
   * @brief Create the window
   * @param config Window configuration
   * @return true if creation succeeded
   */
  bool create(const WindowConfig &config);

  /**
   *@brief Create the window surface and swapchain
   *@param instance for surface creation
   *@param config Swapchain configuration
   *@return tru if creation succeeded
   */
  bool createSwap(const vk::raii::Instance &instance,
                  const SwapchainConfig &config);

  /**
   * @brief Destroy the window
   */
  void destroy();

  /**
   * @brief Check if window is open
   * @return true if window is open
   */
  [[nodiscard]] bool isOpen() const { return window_ != nullptr; }

  /**
   * @brief Check if window should close (close event received)
   * @return true if close was requested
   */
  [[nodiscard]] bool shouldClose() const { return shouldClose_; }

  /**
   * @brief Request window close
   */
  void requestClose() { shouldClose_ = true; }

  /**
   * @brief Poll events for this window
   * @return true if there are more events to process
   */
  bool pollEvents();

  /**
   * @brief Register event callback
   * @param callback Function to call on window events
   */
  void setEventCallback(WindowEventCallback callback,
                        WindowEventType eventType);

  /**
   * @brief Get the SDL window handle
   * @return SDL window pointer
   */
  [[nodiscard]] SDL_Window *getSDLWindow() const { return window_; }

  /**
   * @brief Create Vulkan surface for this window
   * @param instance Vulkan instance
   * @return Vulkan surface
   */
  vk::raii::SurfaceKHR createVulkanSurface(const vk::raii::Instance &instance);

  /**
   * @brief Get required Vulkan instance extensions for windowing
   * @return Vector of required extension names
   */
  static std::vector<std::string> getRequiredVulkanExtensions();

  // Window properties
  [[nodiscard]] int32_t getWidth() const { return width_; }
  [[nodiscard]] int32_t getHeight() const { return height_; }
  [[nodiscard]] float getAspectRatio() const {
    return height_ > 0
               ? static_cast<float>(width_) / static_cast<float>(height_)
               : 1.0f;
  }
  [[nodiscard]] bool isMinimized() const { return minimized_; }
  [[nodiscard]] bool isFocused() const { return focused_; }
  [[nodiscard]] const std::string &getTitle() const { return title_; }

  /**
   * @brief Set window title
   * @param title New window title
   */
  void setTitle(const std::string &title);

  /**
   * @brief Resize window
   * @param width New width
   * @param height New height
   */
  void resize(int32_t width, int32_t height);

  /**
   * @brief Set fullscreen mode
   * @param fullscreen Enable fullscreen
   */
  void setFullscreen(bool fullscreen);

  /**
   * @brief Get window ID (SDL window ID)
   * @return Window ID
   */
  [[nodiscard]] uint32_t getWindowId() const { return windowId_; }

  /**
   * @brief Get main GPU index
   * @return GPU index on DeviceManager vector
   */
  [[nodiscard]] const device::GPUDevice &getMainGPU() const { return *mainGPU; }
  /**
   * @brief Set main GPU index
   */
  void setMainGPU(device::GPUDevice *device);

  /**
   * @brief Get secondary GPUs indexes
   * @return GPU index on DeviceManager vector
   */
  [[nodiscard]] const std::vector<device::GPUDevice *> &
  getSecondaryGPUs() const {
    return secondaryGPUs;
  }
  /**
   * @brief Set secondary GPUs indexes
   */
  void setSecondaryGPUs(std::vector<device::GPUDevice *> &devices);

  /**
   * @brief Get swapchain
   * @return Pointer to Swapchain object or nullptr if not created
   */
  [[nodiscard]] Swapchain *getSwapchain() { return swapchain_.get(); }
  [[nodiscard]] const Swapchain *getSwapchain() const {
    return swapchain_.get();
  }

  /**
   * @brief Check if swapchain is created
   * @return true if swapchain exists
   */
  [[nodiscard]] bool hasSwapchain() const { return swapchain_ != nullptr; }

private:
  SDL_Window *window_ = nullptr;
  uint32_t windowId_ = 0;
  int32_t width_ = 0;
  int32_t height_ = 0;
  std::string title_;

  device::GPUDevice *mainGPU = nullptr;
  std::vector<device::GPUDevice *> secondaryGPUs = {};
  std::unique_ptr<Swapchain> swapchain_;

  bool shouldClose_ = false;
  bool minimized_ = false;
  bool focused_ = true;
  bool fullscreen_ = false;

  std::unordered_map<WindowEventType, std::vector<WindowEventCallback>>
      eventCallback_;
  std::mutex eventCallbackMutex_;

  mutable std::mutex windowMutex_;
};

/**
 * @brief Manages multiple windows
 *
 * Provides centralized window management for multi-window applications.
 */
class WINDOW_API WindowManager {
public:
  WindowManager();
  ~WindowManager();

  // Disable copy
  WindowManager(const WindowManager &) = delete;
  WindowManager &operator=(const WindowManager &) = delete;

  /**
   * @brief Initialize the window manager (initializes SDL)
   * @return true if initialization succeeded
   */
  bool initialize();

  /**
   * @brief Shutdown the window manager
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

  /**
   * @brief Create a new window
   * @param config Window configuration
   * @return Pointer to created window, or nullptr on failure
   */
  Window *createWindow(const WindowConfig &config);

  /**
   * @brief Destroy a specific window
   * @param window Window to destroy
   */
  void destroyWindow(Window *window);

  /**
   * @brief Get window by ID
   * @param windowId SDL window ID
   * @return Pointer to window, or nullptr if not found
   */
  Window *getWindowById(uint32_t windowId);

  /**
   * @brief Get primary window (first created)
   * @return Pointer to primary window
   */
  Window *getPrimaryWindow();

  /**
   * @brief Get all windows
   * @return Vector of window pointers
   */
  [[nodiscard]] const std::vector<std::unique_ptr<Window>> &getWindows() const {
    return windows_;
  }

  /**
   * @brief Get number of windows
   * @return Window count
   */
  [[nodiscard]] size_t getWindowCount() const { return windows_.size(); }

  /**
   * @brief Check if windows vector is empty
   * @return Bool true if empty
   */
  [[nodiscard]] bool checkWindowsVectorEmpty() const {
    return windows_.empty();
  }

  /**
   * @brief Poll events for all windows
   * Uses main loop thread pool if available.
   */
  void pollAllEvents();

  /**
   * @brief Check if any window should close
   * @return true if any window requested close
   */
  [[nodiscard]] bool anyWindowShouldClose() const;

private:
  std::vector<std::unique_ptr<Window>> windows_;
  bool initialized_ = false;
};

} // namespace window

#endif // WINDOW_H_
