#ifndef RENDERER_H_
#define RENDERER_H_

#include "swapchain.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <cstdint>
#include <functional>
#include <memory>

// Forward declare device types
namespace device {
class GPUDevice;
class VMAAllocator;
struct AllocatedImage;
} // namespace device

namespace window {

/**
 * @brief Maximum number of frames that can be in flight simultaneously
 */
constexpr uint32_t MAX_FRAMES_IN_FLIGHT =
    2; // TODO check if the number is optimal or if it could be calculated
       // and change it to the one in the config class

/**
 * @brief Per-frame synchronization resources
 */
struct WINDOW_API FrameSyncObjects {
  std::unique_ptr<vk::raii::Semaphore> imageAvailableSemaphore;
  std::unique_ptr<vk::raii::Semaphore> renderFinishedSemaphore;
  std::unique_ptr<vk::raii::Fence> inFlightFence;
  vk::CommandBuffer commandBuffer; // Owned by command pool
  uint32_t frameIndex = 0;
};

/**
 * @brief Render pass configuration
 */
struct WINDOW_API RenderPassConfig { // TODO check base configuration
  vk::Format colorFormat = vk::Format::eB8G8R8A8Srgb;
  vk::Format depthFormat = vk::Format::eD32Sfloat;
  bool hasDepth = true;
  vk::AttachmentLoadOp colorLoadOp = vk::AttachmentLoadOp::eClear;
  vk::AttachmentStoreOp colorStoreOp = vk::AttachmentStoreOp::eStore;
  vk::AttachmentLoadOp depthLoadOp = vk::AttachmentLoadOp::eClear;
  vk::AttachmentStoreOp depthStoreOp = vk::AttachmentStoreOp::eDontCare;
};

/**
 * @brief Callback for rendering commands
 */
using RenderCallback =
    std::function<void(vk::CommandBuffer cmd, uint32_t imageIndex)>;

/**
 * @brief Manages rendering to a window with synchronization
 *
 * Features:
 * - Frame synchronization with semaphores and fences
 * - Command buffer management
 * - Render pass creation
 * - Multi-threaded rendering support
 */
class WINDOW_API Renderer {
public:
  Renderer();
  ~Renderer();

  // Disable copy
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  /**
   * @brief Initialize the renderer
   * @param device GPU device to use
   * @param swapchain Swapchain to render to
   * @param allocator VMA allocator for depth buffer
   * @return true if initialization succeeded
   */
  bool initialize(device::GPUDevice &device, Swapchain &swapchain,
                  device::VMAAllocator &allocator);

  /**
   * @brief Shutdown the renderer
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

  /**
   * @brief Begin a new frame
   * @return Frame sync objects, or nullptr if frame cannot be started
   */
  FrameSyncObjects *beginFrame();

  /**
   * @brief End the current frame and present
   * @param syncObjects Frame sync objects from beginFrame
   * @param imageIndex Swapchain image index
   * @return true if presentation succeeded
   */
  bool endFrame(FrameSyncObjects &syncObjects, uint32_t imageIndex);

  /**
   * @brief Record commands using a callback
   * @param callback Function that records commands
   */
  void recordCommands(const RenderCallback &callback);

  /**
   * @brief Wait for all frames to complete
   */
  void waitIdle();

  /**
   * @brief Handle swapchain recreation (recreates render pass, depth buffer,
   * etc.)
   * @return true if recreation succeeded
   */
  bool handleSwapchainRecreation();

  // Getters
  [[nodiscard]] vk::RenderPass getRenderPass() const {
    return renderPass_ ? *renderPass_ : vk::RenderPass{};
  }
  [[nodiscard]] uint32_t getCurrentFrame() const { return currentFrame_; }
  [[nodiscard]] uint32_t getFrameCount() const { return MAX_FRAMES_IN_FLIGHT; }

  /**
   * @brief Get current frame's command buffer
   * @return Command buffer for current frame
   */
  [[nodiscard]] vk::CommandBuffer getCurrentCommandBuffer() const;

  /**
   * @brief Submit command buffer to graphics queue
   * @param commandBuffer Command buffer to submit
   * @param waitSemaphores Semaphores to wait on
   * @param signalSemaphores Semaphores to signal
   * @param waitStages Pipeline stages to wait at
   * @param fence Optional fence to signal on completion
   */
  void
  submitCommandBuffer(vk::CommandBuffer commandBuffer,
                      const std::vector<vk::Semaphore> &waitSemaphores,
                      const std::vector<vk::Semaphore> &signalSemaphores,
                      const std::vector<vk::PipelineStageFlags> &waitStages,
                      vk::Fence fence = nullptr);

  /**
   * @brief Get clear color for render pass
   * @return Clear color value
   */
  [[nodiscard]] vk::ClearColorValue getClearColor() const {
    return clearColor_;
  }

  /**
   * @brief Set clear color for render pass
   * @param color New clear color
   */
  void setClearColor(const vk::ClearColorValue &color) { clearColor_ = color; }

  /**
   * @brief Get clear values array for render pass begin
   * @return Array of clear values (color + depth)
   */
  [[nodiscard]] std::array<vk::ClearValue, 2> getClearValues() const;

private:
  bool createRenderPass(const RenderPassConfig &config);
  void destroyRenderPass();
  bool createCommandPool();
  void destroyCommandPool();
  bool createCommandBuffers();
  void destroyCommandBuffers();
  bool createSyncObjects();
  void destroySyncObjects();
  bool createDepthResources();
  void destroyDepthResources();

  device::GPUDevice *gpuDevice_ = nullptr;
  Swapchain *swapchain_ = nullptr;
  device::VMAAllocator *allocator_ = nullptr;

  std::unique_ptr<vk::raii::RenderPass> renderPass_;
  std::unique_ptr<vk::raii::CommandPool> commandPool_;
  std::vector<FrameSyncObjects> frameSyncObjects_;

  // Depth buffer
  std::unique_ptr<device::AllocatedImage> depthImage_;

  uint32_t currentFrame_ = 0;
  uint32_t currentImageIndex_ = 0;

  vk::ClearColorValue clearColor_ =
      std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};

  bool initialized_ = false;
  mutable std::mutex renderMutex_;
};

} // namespace window

#endif // RENDERER_H_
