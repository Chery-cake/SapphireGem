#include "renderer.h"
#include "vulkan_device.h"
#include "vma_allocator.h"
#include <iostream>

namespace window {

Renderer::Renderer() = default;

Renderer::~Renderer() {
    shutdown();
}

bool Renderer::initialize(device::GPUDevice& device, Swapchain& swapchain, device::VMAAllocator& allocator) {
    if (initialized_) {
        std::cerr << "[Renderer] Already initialized" << std::endl;
        return false;
    }

    device_ = device.getDevice();
    gpuDevice_ = &device;
    swapchain_ = &swapchain;
    allocator_ = &allocator;

    // Create render pass
    RenderPassConfig renderPassConfig;
    renderPassConfig.colorFormat = swapchain.getFormat();
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
    vk::ImageView depthView = depthImage_ ? depthImage_->view : nullptr;
    if (!swapchain_->createFramebuffers(renderPass_, depthView)) {
        destroyDepthResources();
        destroySyncObjects();
        destroyCommandBuffers();
        destroyCommandPool();
        destroyRenderPass();
        return false;
    }

    initialized_ = true;
    std::cout << "[Renderer] Initialized successfully" << std::endl;
    return true;
}

void Renderer::shutdown() {
    if (!initialized_) {
        return;
    }

    waitIdle();

    if (swapchain_) {
        swapchain_->destroyFramebuffers();
    }

    destroyDepthResources();
    destroySyncObjects();
    destroyCommandBuffers();
    destroyCommandPool();
    destroyRenderPass();

    device_ = nullptr;
    gpuDevice_ = nullptr;
    swapchain_ = nullptr;
    allocator_ = nullptr;
    initialized_ = false;

    std::cout << "[Renderer] Shutdown complete" << std::endl;
}

bool Renderer::createRenderPass(const RenderPassConfig& config) {
    // Color attachment
    vk::AttachmentDescription colorAttachment{
        {},
        config.colorFormat,
        vk::SampleCountFlagBits::e1,
        config.colorLoadOp,
        config.colorStoreOp,
        vk::AttachmentLoadOp::eDontCare,
        vk::AttachmentStoreOp::eDontCare,
        vk::ImageLayout::eUndefined,
        vk::ImageLayout::ePresentSrcKHR
    };

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
        vk::ImageLayout::eDepthStencilAttachmentOptimal
    };

    vk::AttachmentReference colorRef{0, vk::ImageLayout::eColorAttachmentOptimal};
    vk::AttachmentReference depthRef{1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

    vk::SubpassDescription subpass{
        {},
        vk::PipelineBindPoint::eGraphics,
        {},
        colorRef,
        {},
        config.hasDepth ? &depthRef : nullptr
    };

    // Subpass dependency
    vk::SubpassDependency dependency{
        VK_SUBPASS_EXTERNAL,
        0,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests,
        {},
        vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite
    };

    std::vector<vk::AttachmentDescription> attachments = {colorAttachment};
    if (config.hasDepth) {
        attachments.push_back(depthAttachment);
    }

    vk::RenderPassCreateInfo renderPassInfo{
        {},
        attachments,
        subpass,
        dependency
    };

    try {
        renderPass_ = device_.createRenderPass(renderPassInfo);
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to create render pass: " << e.what() << std::endl;
        return false;
    }
}

void Renderer::destroyRenderPass() {
    if (renderPass_) {
        device_.destroyRenderPass(renderPass_);
        renderPass_ = nullptr;
    }
}

bool Renderer::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo{
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        gpuDevice_->getQueueFamilies().graphicsFamily.value_or(0)
    };

    try {
        commandPool_ = device_.createCommandPool(poolInfo);
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to create command pool: " << e.what() << std::endl;
        return false;
    }
}

void Renderer::destroyCommandPool() {
    if (commandPool_) {
        device_.destroyCommandPool(commandPool_);
        commandPool_ = nullptr;
    }
}

bool Renderer::createCommandBuffers() {
    vk::CommandBufferAllocateInfo allocInfo{
        commandPool_,
        vk::CommandBufferLevel::ePrimary,
        MAX_FRAMES_IN_FLIGHT
    };

    try {
        auto commandBuffers = device_.allocateCommandBuffers(allocInfo);
        frameSyncObjects_.resize(MAX_FRAMES_IN_FLIGHT);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            frameSyncObjects_[i].commandBuffer = commandBuffers[i];
            frameSyncObjects_[i].frameIndex = i;
        }
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to allocate command buffers: " << e.what() << std::endl;
        return false;
    }
}

void Renderer::destroyCommandBuffers() {
    // Command buffers are automatically freed when pool is destroyed
    frameSyncObjects_.clear();
}

bool Renderer::createSyncObjects() {
    vk::SemaphoreCreateInfo semaphoreInfo{};
    vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};

    try {
        for (auto& sync : frameSyncObjects_) {
            sync.imageAvailableSemaphore = device_.createSemaphore(semaphoreInfo);
            sync.renderFinishedSemaphore = device_.createSemaphore(semaphoreInfo);
            sync.inFlightFence = device_.createFence(fenceInfo);
        }
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to create sync objects: " << e.what() << std::endl;
        return false;
    }
}

void Renderer::destroySyncObjects() {
    for (auto& sync : frameSyncObjects_) {
        if (sync.imageAvailableSemaphore) {
            device_.destroySemaphore(sync.imageAvailableSemaphore);
        }
        if (sync.renderFinishedSemaphore) {
            device_.destroySemaphore(sync.renderFinishedSemaphore);
        }
        if (sync.inFlightFence) {
            device_.destroyFence(sync.inFlightFence);
        }
    }
}

bool Renderer::createDepthResources() {
    if (!allocator_) {
        return false;
    }

    device::ImageCreateInfo imageInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = vk::Format::eD32Sfloat;
    imageInfo.extent = vk::Extent3D{swapchain_->getExtent().width, swapchain_->getExtent().height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    imageInfo.memoryUsage = vma::MemoryUsage::eGpuOnly;
    imageInfo.debugName = "DepthBuffer";

    depthImage_ = std::make_unique<device::AllocatedImage>(allocator_->createImage(imageInfo));
    if (!depthImage_->isValid()) {
        std::cerr << "[Renderer] Failed to create depth image" << std::endl;
        return false;
    }

    if (!allocator_->createImageView(*depthImage_, vk::ImageAspectFlagBits::eDepth)) {
        std::cerr << "[Renderer] Failed to create depth image view" << std::endl;
        allocator_->destroyImage(*depthImage_);
        depthImage_.reset();
        return false;
    }

    return true;
}

void Renderer::destroyDepthResources() {
    if (depthImage_ && allocator_) {
        allocator_->destroyImage(*depthImage_);
        depthImage_.reset();
    }
}

FrameSyncObjects* Renderer::beginFrame() {
    std::lock_guard<std::mutex> lock(renderMutex_);

    auto& sync = frameSyncObjects_[currentFrame_];

    // Wait for this frame's fence
    auto result = device_.waitForFences(sync.inFlightFence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        std::cerr << "[Renderer] Failed to wait for fence" << std::endl;
        return nullptr;
    }

    // Acquire next image
    currentImageIndex_ = swapchain_->acquireNextImage(sync.imageAvailableSemaphore);
    if (currentImageIndex_ == UINT32_MAX) {
        // Swapchain needs recreation
        return nullptr;
    }

    // Reset fence only after successful acquire
    device_.resetFences(sync.inFlightFence);

    // Reset and begin command buffer
    sync.commandBuffer.reset();
    vk::CommandBufferBeginInfo beginInfo{};
    sync.commandBuffer.begin(beginInfo);

    return &sync;
}

bool Renderer::endFrame(FrameSyncObjects& syncObjects, uint32_t imageIndex) {
    std::lock_guard<std::mutex> lock(renderMutex_);

    // End command buffer
    syncObjects.commandBuffer.end();

    // Submit command buffer
    std::vector<vk::Semaphore> waitSemaphores = {syncObjects.imageAvailableSemaphore};
    std::vector<vk::PipelineStageFlags> waitStages = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    std::vector<vk::Semaphore> signalSemaphores = {syncObjects.renderFinishedSemaphore};

    vk::SubmitInfo submitInfo{
        waitSemaphores,
        waitStages,
        syncObjects.commandBuffer,
        signalSemaphores
    };

    try {
        gpuDevice_->getGraphicsQueue().submit(submitInfo, syncObjects.inFlightFence);
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to submit command buffer: " << e.what() << std::endl;
        return false;
    }

    // Present
    bool presentResult = swapchain_->present(imageIndex, signalSemaphores);

    // Advance frame
    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;

    return presentResult;
}

void Renderer::recordCommands(const RenderCallback& callback) {
    if (!initialized_) {
        return;
    }

    auto& sync = frameSyncObjects_[currentFrame_];
    callback(sync.commandBuffer, currentImageIndex_);
}

void Renderer::waitIdle() {
    if (device_) {
        device_.waitIdle();
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
    vk::ImageView depthView = depthImage_ ? depthImage_->view : nullptr;
    return swapchain_->createFramebuffers(renderPass_, depthView);
}

vk::CommandBuffer Renderer::getCurrentCommandBuffer() const {
    if (frameSyncObjects_.empty()) {
        return nullptr;
    }
    return frameSyncObjects_[currentFrame_].commandBuffer;
}

void Renderer::submitCommandBuffer(vk::CommandBuffer commandBuffer,
                                   const std::vector<vk::Semaphore>& waitSemaphores,
                                   const std::vector<vk::Semaphore>& signalSemaphores,
                                   const std::vector<vk::PipelineStageFlags>& waitStages,
                                   vk::Fence fence) {
    vk::SubmitInfo submitInfo{
        waitSemaphores,
        waitStages,
        commandBuffer,
        signalSemaphores
    };

    try {
        gpuDevice_->getGraphicsQueue().submit(submitInfo, fence);
    } catch (const vk::SystemError& e) {
        std::cerr << "[Renderer] Failed to submit command buffer: " << e.what() << std::endl;
    }
}

std::array<vk::ClearValue, 2> Renderer::getClearValues() const {
    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color = clearColor_;
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    return clearValues;
}

} // namespace window
