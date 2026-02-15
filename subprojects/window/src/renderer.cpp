#include "renderer.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
#include <cstdint>
#include <print>

namespace window {

Renderer::Renderer() = default;

Renderer::~Renderer() { shutdown(); }

void Renderer::shutdown() {
  if (!initialized_) {
    return;
  }

  waitIdle();

  // Destroy framebuffers first (they reference the render pass)
  if (swapchain_) {
    swapchain_->destroyFramebuffers();
  }

  destroyDepthResources();
  destroySyncObjects();
  destroyCommandBuffers();
  destroyCommandPool();
  destroyRenderPass();

  // Destroy swapchain
  swapchain_.reset();

  gpuDevice_ = nullptr;
  swapchain_ = nullptr;
  allocator_ = nullptr;
  initialized_ = false;

  std::println("[Renderer] Shutdown complete");
}

bool Renderer::initialize(device::GPUDevice &device,
                          std::vector<device::GPUDevice *> &secondaryGPUs,
                          vk::raii::SurfaceKHR &surface,
                          const SwapchainConfig &swapConfig,
                          device::VMAAllocator &allocator, uint32_t windowWidth,
                          uint32_t windowHeight) {
  if (initialized_) {
    std::println(stderr, "[Renderer] Already initialized");
    return false;
  }

  gpuDevice_ = &device;
  allocator_ = &allocator;

  // Create and own the swapchain
  swapchain_ = std::make_unique<Swapchain>();
  if (!swapchain_->create(&device, secondaryGPUs, surface, swapConfig,
                          windowWidth, windowHeight)) {
    std::println(stderr,
                 "[Renderer] Failed to create swapchain during initialization");
    swapchain_.reset();
    return false;
  }

  // Create render pass
  RenderPassConfig renderPassConfig;
  renderPassConfig.colorFormat = swapchain_->getFormat();
  if (!createRenderPass(renderPassConfig)) {
    return false;
  }

  // Create command pool
  if (!createCommandPool()) {
    destroyRenderPass();
    return false;
  }

  // Create command buffers
  if (!createCommandBuffers()) {
    destroyCommandPool();
    destroyRenderPass();
    return false;
  }

  // Create sync objects
  if (!createSyncObjects()) {
    destroyCommandBuffers();
    destroyCommandPool();
    destroyRenderPass();
    return false;
  }

  // Create depth resources
  if (!createDepthResources()) {
    destroySyncObjects();
    destroyCommandBuffers();
    destroyCommandPool();
    destroyRenderPass();
    return false;
  }

  // Create framebuffers
  vk::ImageView depthView = depthImage_ ? **depthImage_->view : nullptr;
  if (!swapchain_->createFramebuffers(getRenderPass(), depthView)) {
    destroyDepthResources();
    destroySyncObjects();
    destroyCommandBuffers();
    destroyCommandPool();
    destroyRenderPass();
    return false;
  }

  initialized_ = true;
  std::println("[Renderer] Initialized successfully");
  return true;
}

bool Renderer::createRenderPass(const RenderPassConfig &config) {
  if (!gpuDevice_) {
    return false;
  }

  // Color attachment
  vk::AttachmentDescription colorAttachment{{},
                                            config.colorFormat,
                                            vk::SampleCountFlagBits::e1,
                                            config.colorLoadOp,
                                            config.colorStoreOp,
                                            vk::AttachmentLoadOp::eDontCare,
                                            vk::AttachmentStoreOp::eDontCare,
                                            vk::ImageLayout::eUndefined,
                                            vk::ImageLayout::ePresentSrcKHR};

  // Depth attachment
  vk::AttachmentDescription depthAttachment{
      {},
      config.depthFormat,
      vk::SampleCountFlagBits::e1,
      config.depthLoadOp,
      config.depthStoreOp,
      vk::AttachmentLoadOp::eDontCare,
      vk::AttachmentStoreOp::eDontCare,
      vk::ImageLayout::eUndefined,
      vk::ImageLayout::eDepthStencilAttachmentOptimal};

  vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
  vk::AttachmentReference depthRef{
      1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

  vk::SubpassDescription subpass{{}, vk::PipelineBindPoint::eGraphics,
                                 {}, colorRef,
                                 {}, config.hasDepth ? &depthRef : nullptr};

  // Subpass dependency
  vk::SubpassDependency dependency{
      vk::SubpassExternal,
      0,
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
          vk::PipelineStageFlagBits::eEarlyFragmentTests,
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
          vk::PipelineStageFlagBits::eEarlyFragmentTests,
      {},
      vk::AccessFlagBits::eColorAttachmentWrite |
          vk::AccessFlagBits::eDepthStencilAttachmentWrite};

  std::vector<vk::AttachmentDescription> attachments = {colorAttachment};
  if (config.hasDepth) {
    attachments.push_back(depthAttachment);
  }

  vk::RenderPassCreateInfo renderPassInfo{{}, attachments, subpass, dependency};

  try {
    renderPass_ = std::make_unique<vk::raii::RenderPass>(
        gpuDevice_->getRaiiDevice(), renderPassInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Renderer] Failed to create render pass: {}",
                 e.what());
    return false;
  }
}

void Renderer::destroyRenderPass() { renderPass_.reset(); }

bool Renderer::createCommandPool() {
  if (!gpuDevice_) {
    return false;
  }

  vk::CommandPoolCreateInfo poolInfo{
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      gpuDevice_->getQueueFamilies().graphicsFamily.value_or(0)};

  try {
    commandPool_ = std::make_unique<vk::raii::CommandPool>(
        gpuDevice_->getRaiiDevice(), poolInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Renderer] Failed to create command pool: {}",
                 e.what());
    return false;
  }
}

void Renderer::destroyCommandPool() { commandPool_.reset(); }

bool Renderer::createCommandBuffers() {
  if (!gpuDevice_ || !commandPool_) {
    return false;
  }

  vk::CommandBufferAllocateInfo allocInfo{
      *commandPool_, vk::CommandBufferLevel::ePrimary, MAX_FRAMES_IN_FLIGHT};

  try {
    // Use raw device for allocation since command buffers are managed by
    // pool
    auto commandBuffers =
        gpuDevice_->getDevice().allocateCommandBuffers(allocInfo);
    frameSyncObjects_.resize(MAX_FRAMES_IN_FLIGHT);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      frameSyncObjects_[i].commandBuffer = commandBuffers[i];
      frameSyncObjects_[i].frameIndex = i;
    }
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Renderer] Failed to allocate command buffers: {}",
                 e.what());
    return false;
  }
}

void Renderer::destroyCommandBuffers() {
  // Command buffers are automatically freed when pool is destroyed
  frameSyncObjects_.clear();
}

bool Renderer::createSyncObjects() {
  if (!gpuDevice_) {
    return false;
  }

  vk::SemaphoreCreateInfo semaphoreInfo{};
  vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};

  try {
    for (auto &sync : frameSyncObjects_) {
      sync.imageAvailableSemaphore = std::make_unique<vk::raii::Semaphore>(
          gpuDevice_->getRaiiDevice(), semaphoreInfo);
      sync.renderFinishedSemaphore = std::make_unique<vk::raii::Semaphore>(
          gpuDevice_->getRaiiDevice(), semaphoreInfo);
      sync.inFlightFence = std::make_unique<vk::raii::Fence>(
          gpuDevice_->getRaiiDevice(), fenceInfo);
    }
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Renderer] Failed to create sync objects: {}",
                 e.what());
    return false;
  }
}

void Renderer::destroySyncObjects() {
  for (auto &sync : frameSyncObjects_) {
    sync.imageAvailableSemaphore.reset();
    sync.renderFinishedSemaphore.reset();
    sync.inFlightFence.reset();
  }
}

bool Renderer::createDepthResources() {
  if (!allocator_) {
    return false;
  }

  device::ImageCreateInfo imageInfo{};
  imageInfo.imageType =
      vk::ImageType::e2D; // TODO check if can be 3D
                          // would need to adapt swapchain extent to 3D
  imageInfo.format = vk::Format::eD32Sfloat;
  imageInfo.extent = vk::Extent3D{swapchain_->getExtent().width,
                                  swapchain_->getExtent().height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
  imageInfo.memoryUsage = vma::MemoryUsage::eGpuOnly;
  imageInfo.debugName = "DepthBuffer";

  depthImage_ = std::make_unique<device::AllocatedImage>(
      allocator_->createImage(imageInfo));
  if (!depthImage_->isValid()) {
    std::println(stderr, "[Renderer] Failed to create depth image");
    return false;
  }

  if (!allocator_->createImageView(*depthImage_,
                                   vk::ImageAspectFlagBits::eDepth)) {
    std::println(stderr, "[Renderer] Failed to create depth image view");
    depthImage_.reset();
    return false;
  }

  return true;
}

void Renderer::destroyDepthResources() {
  if (depthImage_ && allocator_) {
    depthImage_.reset();
  }
}

FrameSyncObjects *Renderer::beginFrame() {
  std::lock_guard<std::mutex> lock(renderMutex_);

  if (!gpuDevice_ || frameSyncObjects_.empty()) {
    return nullptr;
  }

  auto &sync = frameSyncObjects_[currentFrame_];

  // Wait for this frame's fence
  auto result = gpuDevice_->getDevice().waitForFences(**sync.inFlightFence,
                                                      vk::True, UINT64_MAX);
  if (result != vk::Result::eSuccess) {
    std::println(stderr, "[Renderer] Failed to wait for fence");
    return nullptr;
  }

  // Acquire next image
  currentImageIndex_ =
      swapchain_->acquireNextImage(*sync.imageAvailableSemaphore);
  if (currentImageIndex_ == UINT32_MAX) {
    // Swapchain needs recreation
    // TODO add swapchain recreation call
    return nullptr;
  }

  // Reset fence only after successful acquire
  gpuDevice_->getDevice().resetFences(**sync.inFlightFence);

  // Reset and begin command buffer
  sync.commandBuffer.reset();
  vk::CommandBufferBeginInfo beginInfo{}; // TODO check if need to add info
  sync.commandBuffer.begin(beginInfo);

  return &sync;
}

bool Renderer::endFrame(FrameSyncObjects &syncObjects, uint32_t imageIndex) {
  std::lock_guard<std::mutex> lock(renderMutex_);

  if (!gpuDevice_) {
    return false;
  }

  // End command buffer
  syncObjects.commandBuffer.end();

  // Submit command buffer
  vk::Semaphore waitSemaphore = **syncObjects.imageAvailableSemaphore;
  vk::Semaphore signalSemaphore = **syncObjects.renderFinishedSemaphore;
  std::vector<vk::Semaphore> waitSemaphores = {waitSemaphore};
  std::vector<vk::PipelineStageFlags> waitStages = {
      vk::PipelineStageFlagBits::eColorAttachmentOutput};
  std::vector<vk::Semaphore> signalSemaphores = {signalSemaphore};

  submitCommandBuffer(syncObjects.commandBuffer, waitSemaphores,
                      signalSemaphores, waitStages,
                      **syncObjects.inFlightFence);

  // Present
  bool presentResult = swapchain_->present(imageIndex, signalSemaphores);

  // Advance frame
  currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;

  return presentResult;
}

void Renderer::recordCommands(const RenderCallback &callback) {
  if (!initialized_) {
    return;
  }

  auto &sync = frameSyncObjects_[currentFrame_];
  callback(sync.commandBuffer, currentImageIndex_);
}

void Renderer::waitIdle() {
  if (gpuDevice_) {
    gpuDevice_->waitIdle();
  }
}

bool Renderer::handleSwapchainRecreation() {
  waitIdle();

  // Destroy framebuffers
  swapchain_->destroyFramebuffers();

  // Destroy and recreate depth resources
  destroyDepthResources();
  if (!createDepthResources()) {
    return false;
  }

  // Recreate framebuffers
  vk::ImageView depthView = depthImage_ ? **depthImage_->view : nullptr;
  return swapchain_->createFramebuffers(getRenderPass(), depthView);
}

vk::CommandBuffer Renderer::getCurrentCommandBuffer() const {
  if (frameSyncObjects_.empty()) {
    return nullptr;
  }
  return frameSyncObjects_[currentFrame_].commandBuffer;
}

void Renderer::submitCommandBuffer(
    vk::CommandBuffer commandBuffer,
    const std::vector<vk::Semaphore> &waitSemaphores,
    const std::vector<vk::Semaphore> &signalSemaphores,
    const std::vector<vk::PipelineStageFlags> &waitStages, vk::Fence fence) {
  if (!gpuDevice_) {
    return;
  }

  vk::SubmitInfo submitInfo{waitSemaphores, waitStages, commandBuffer,
                            signalSemaphores};

  try {
    gpuDevice_->getGraphicsQueue().submit(submitInfo, fence);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Renderer] Failed to submit command buffer: {}",
                 e.what());
  }
}

std::array<vk::ClearValue, 2> Renderer::getClearValues() const {
  std::array<vk::ClearValue, 2> clearValues;
  clearValues[0].color = clearColor_;
  clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
  return clearValues;
}

} // namespace window
