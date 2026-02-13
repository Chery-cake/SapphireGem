#ifndef SWAPCHAIN_H_
#define SWAPCHAIN_H_

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Forward declare device types
namespace device {
class GPUDevice;
}

namespace window {

/**
 * @brief Swapchain configuration
 */
struct WINDOW_API SwapchainConfig { // TODO check default config
  vk::PresentModeKHR preferredPresentMode = vk::PresentModeKHR::eFifo; // VSync
  vk::Format preferredFormat = vk::Format::eB8G8R8A8Srgb;
  vk::ColorSpaceKHR preferredColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
  uint32_t minImageCount = 2; // Double buffering by default
  bool vsync = true;
};

/**
 * @brief Swapchain image resources using RAII
 */
struct WINDOW_API SwapchainFrame {
  vk::Image image; // Owned by swapchain
  std::unique_ptr<vk::raii::ImageView> imageView;
  std::unique_ptr<vk::raii::Framebuffer> framebuffer;
  uint32_t index = 0;
};

/**
 * @brief Manages Vulkan swapchain for a window using RAII
 *
 * Uses vk::raii wrappers for automatic resource management.
 * Handles:
 * - Swapchain creation and recreation
 * - Image acquisition and presentation
 * - Framebuffer management
 */
class WINDOW_API Swapchain {
public:
  Swapchain();
  ~Swapchain();

  // Disable copy
  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;

  // Enable move
  Swapchain(Swapchain &&other) noexcept;
  Swapchain &operator=(Swapchain &&other) noexcept;

  /**
   * @brief Create the swapchain
   * @param device GPU device to use
   * @param surface Vulkan surface
   * @param config Swapchain configuration
   * @return true if creation succeeded
   */
  bool create(device::GPUDevice *device,
              std::vector<device::GPUDevice *> &secondary,
              vk::raii::SurfaceKHR &surface, const SwapchainConfig &config);

  /**
   * @brief Destroy the swapchain
   */
  void destroy();

  /**
   * @brief Recreate swapchain (e.g., after resize)
   * @param newWidth New width
   * @param newHeight New height
   * @return true if recreation succeeded
   */
  bool recreate(uint32_t newWidth, uint32_t newHeight);

  /**
   * @brief Check if swapchain is valid
   * @return true if valid
   */
  [[nodiscard]] bool isValid() const { return swapchain_ && !needsRecreation_; }

  /**
   * @brief Check if swapchain needs recreation
   * @return true if recreation is needed
   */
  [[nodiscard]] bool needsRecreation() const { return needsRecreation_; }

  /**
   * @brief Mark swapchain as needing recreation
   */
  void markForRecreation() { needsRecreation_ = true; }

  /**
   * @brief Acquire next image from swapchain
   * @param semaphore Semaphore to signal when image is acquired
   * @param fence Optional fence to signal
   * @param timeout Timeout in nanoseconds (default: infinite)
   * @return Image index, or UINT32_MAX on failure
   */
  uint32_t acquireNextImage(vk::Semaphore semaphore, vk::Fence fence = nullptr,
                            uint64_t timeout = UINT64_MAX);

  /**
   * @brief Present an image to the swapchain
   * @param imageIndex Index of image to present
   * @param waitSemaphores Semaphores to wait on before presenting
   * @return true if presentation succeeded
   */
  bool present(uint32_t imageIndex,
               const std::vector<vk::Semaphore> &waitSemaphores);

  // Getters
  [[nodiscard]] vk::SwapchainKHR getSwapchain() const {
    return swapchain_ ? **swapchain_ : vk::SwapchainKHR{};
  }
  [[nodiscard]] vk::Format getFormat() const { return format_; }
  [[nodiscard]] vk::Extent2D getExtent() const { return extent_; }
  [[nodiscard]] uint32_t getImageCount() const {
    return static_cast<uint32_t>(frames_.size());
  }
  [[nodiscard]] const std::vector<SwapchainFrame> &getFrames() const {
    return frames_;
  }
  [[nodiscard]] const SwapchainFrame &getFrame(uint32_t index) const {
    return frames_[index];
  }

  /**
   * @brief Create framebuffers for all swapchain images
   * @param renderPass Render pass to create framebuffers for
   * @param depthView Optional depth attachment view
   * @return true if creation succeeded
   */
  bool createFramebuffers(vk::RenderPass renderPass,
                          vk::ImageView depthView = nullptr);

  /**
   * @brief Destroy all framebuffers
   */
  void destroyFramebuffers();

  /**
   * @brief Create offscreen images for secondary GPUs
   *
   * Creates images on secondary GPUs that can be used for rendering
   * and later transferred/composed to the main swapchain for presentation.
   *
   * @return true if creation succeeded
   */
  bool createOffscreenImagesForSecondaryGPUs();

  /**
   * @brief Destroy all offscreen images
   */
  void destroyOffscreenImages();

  /**
   * @brief Get offscreen image for a specific secondary GPU
   * @param gpuIndex Index of the secondary GPU
   * @return Pointer to the offscreen image, or nullptr if not found
   */
  [[nodiscard]] const vk::raii::Image *
  getOffscreenImage(uint32_t gpuIndex) const;

  /**
   * @brief Check if offscreen images exist for secondary GPUs
   * @return true if offscreen images have been created
   */
  [[nodiscard]] bool hasOffscreenImages() const {
    return !offscreenImages_.empty();
  }

  /**
   * @brief Get count of offscreen images
   * @return Number of offscreen images
   */
  [[nodiscard]] size_t getOffscreenImageCount() const {
    return offscreenImages_.size();
  }

private:
  static vk::SurfaceFormatKHR
  chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &formats,
                      const SwapchainConfig &config);
  static vk::PresentModeKHR
  choosePresentMode(const std::vector<vk::PresentModeKHR> &modes,
                    const SwapchainConfig &config);
  static vk::Extent2D
  chooseExtent(const vk::SurfaceCapabilitiesKHR &capabilities,
               uint32_t windowWidth, uint32_t windowHeight);

  void createImageViews();
  void destroyImageViews();

  device::GPUDevice *mainGPUDevice_ = nullptr;
  std::vector<device::GPUDevice *> secondaryGPUDevices_;
  std::unique_ptr<vk::raii::SurfaceKHR> surface_;
  // Note: vk::Queue is a handle that doesn't own the underlying resource.
  // Queues are managed by the VkDevice and don't need explicit destruction.
  // Using raw vk::Queue is correct here; std::unique_ptr<vk::raii::Queue>
  // would be unnecessary since vk::raii::Queue is also a non-owning wrapper.
  vk::Queue presentQueue_;
  uint32_t presentQueueFamily_ = 0;

  std::unique_ptr<vk::raii::SwapchainKHR> swapchain_;
  // Maps secondary GPU index to offscreen image for multi-GPU rendering.
  // Secondary GPUs render to these offscreen images, which are then
  // transferred/composed to the main swapchain for presentation.
  std::unordered_map<uint32_t, std::unique_ptr<vk::raii::Image>>
      offscreenImages_;

  vk::Format format_ = vk::Format::eUndefined;
  vk::Extent2D extent_ = {0, 0};

  std::vector<SwapchainFrame> frames_;
  SwapchainConfig config_;

  bool needsRecreation_ = false;

  mutable std::mutex swapchainMutex_;
};

} // namespace window

#endif // SWAPCHAIN_H_
