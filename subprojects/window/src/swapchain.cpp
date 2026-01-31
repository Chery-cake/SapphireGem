#include "swapchain.h"
#include "vulkan_device.h"
#include <algorithm>
#include <iostream>

namespace window {

Swapchain::Swapchain() = default;

Swapchain::~Swapchain() {
    destroy();
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : device_(other.device_)
    , physicalDevice_(other.physicalDevice_)
    , surface_(other.surface_)
    , presentQueue_(other.presentQueue_)
    , presentQueueFamily_(other.presentQueueFamily_)
    , swapchain_(other.swapchain_)
    , format_(other.format_)
    , extent_(other.extent_)
    , frames_(std::move(other.frames_))
    , config_(other.config_)
    , needsRecreation_(other.needsRecreation_) {
    other.swapchain_ = nullptr;
    other.device_ = nullptr;
}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = other.device_;
        physicalDevice_ = other.physicalDevice_;
        surface_ = other.surface_;
        presentQueue_ = other.presentQueue_;
        presentQueueFamily_ = other.presentQueueFamily_;
        swapchain_ = other.swapchain_;
        format_ = other.format_;
        extent_ = other.extent_;
        frames_ = std::move(other.frames_);
        config_ = other.config_;
        needsRecreation_ = other.needsRecreation_;
        other.swapchain_ = nullptr;
        other.device_ = nullptr;
    }
    return *this;
}

bool Swapchain::create(device::GPUDevice& device, vk::SurfaceKHR surface, const SwapchainConfig& config) {
    if (swapchain_) {
        std::cerr << "[Swapchain] Already created" << std::endl;
        return false;
    }

    device_ = device.getDevice();
    physicalDevice_ = device.getPhysicalDevice();
    surface_ = surface;
    presentQueue_ = device.getPresentQueue();
    presentQueueFamily_ = device.getQueueFamilies().presentFamily.value_or(0);
    config_ = config;

    // Query swapchain support
    auto capabilities = physicalDevice_.getSurfaceCapabilitiesKHR(surface_);
    auto formats = physicalDevice_.getSurfaceFormatsKHR(surface_);
    auto presentModes = physicalDevice_.getSurfacePresentModesKHR(surface_);

    if (formats.empty() || presentModes.empty()) {
        std::cerr << "[Swapchain] Inadequate swapchain support" << std::endl;
        return false;
    }

    // Choose format, present mode, and extent
    auto surfaceFormat = chooseSurfaceFormat(formats, config);
    auto presentMode = choosePresentMode(presentModes, config);
    extent_ = chooseExtent(capabilities, capabilities.currentExtent.width, capabilities.currentExtent.height);
    format_ = surfaceFormat.format;

    // Determine image count
    uint32_t imageCount = std::max(config.minImageCount, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    // Create swapchain
    vk::SwapchainCreateInfoKHR createInfo{
        {},
        surface_,
        imageCount,
        format_,
        surfaceFormat.colorSpace,
        extent_,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        {},
        capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        presentMode,
        VK_TRUE,
        nullptr  // Old swapchain
    };

    // Handle multiple queue families
    uint32_t graphicsFamily = device.getQueueFamilies().graphicsFamily.value_or(0);
    if (graphicsFamily != presentQueueFamily_) {
        std::array<uint32_t, 2> queueFamilyIndices = {graphicsFamily, presentQueueFamily_};
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }

    try {
        swapchain_ = device_.createSwapchainKHR(createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[Swapchain] Failed to create swapchain: " << e.what() << std::endl;
        return false;
    }

    // Get swapchain images and create views
    auto images = device_.getSwapchainImagesKHR(swapchain_);
    frames_.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        frames_[i].image = images[i];
        frames_[i].index = static_cast<uint32_t>(i);
    }

    createImageViews();
    needsRecreation_ = false;

    std::cout << "[Swapchain] Created: " << extent_.width << "x" << extent_.height
              << " (" << frames_.size() << " images)" << std::endl;
    return true;
}

void Swapchain::destroy() {
    if (!device_) {
        return;
    }

    device_.waitIdle();

    destroyFramebuffers();
    destroyImageViews();

    if (swapchain_) {
        device_.destroySwapchainKHR(swapchain_);
        swapchain_ = nullptr;
    }

    frames_.clear();
    std::cout << "[Swapchain] Destroyed" << std::endl;
}

bool Swapchain::recreate(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == 0 || newHeight == 0) {
        return false;  // Window minimized
    }

    device_.waitIdle();

    // Store old swapchain
    vk::SwapchainKHR oldSwapchain = swapchain_;

    // Query updated capabilities
    auto capabilities = physicalDevice_.getSurfaceCapabilitiesKHR(surface_);
    auto formats = physicalDevice_.getSurfaceFormatsKHR(surface_);
    auto presentModes = physicalDevice_.getSurfacePresentModesKHR(surface_);

    auto surfaceFormat = chooseSurfaceFormat(formats, config_);
    auto presentMode = choosePresentMode(presentModes, config_);
    extent_ = chooseExtent(capabilities, newWidth, newHeight);
    format_ = surfaceFormat.format;

    uint32_t imageCount = std::max(config_.minImageCount, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    vk::SwapchainCreateInfoKHR createInfo{
        {},
        surface_,
        imageCount,
        format_,
        surfaceFormat.colorSpace,
        extent_,
        1,
        vk::ImageUsageFlagBits::eColorAttachment,
        vk::SharingMode::eExclusive,
        {},
        capabilities.currentTransform,
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        presentMode,
        VK_TRUE,
        oldSwapchain
    };

    // Destroy old image views first
    destroyImageViews();

    try {
        swapchain_ = device_.createSwapchainKHR(createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[Swapchain] Failed to recreate swapchain: " << e.what() << std::endl;
        swapchain_ = oldSwapchain;  // Restore old
        return false;
    }

    // Destroy old swapchain
    if (oldSwapchain) {
        device_.destroySwapchainKHR(oldSwapchain);
    }

    // Get new images
    auto images = device_.getSwapchainImagesKHR(swapchain_);
    frames_.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        frames_[i].image = images[i];
        frames_[i].index = static_cast<uint32_t>(i);
        frames_[i].framebuffer = nullptr;  // Needs recreation
    }

    createImageViews();
    needsRecreation_ = false;

    std::cout << "[Swapchain] Recreated: " << extent_.width << "x" << extent_.height << std::endl;
    return true;
}

uint32_t Swapchain::acquireNextImage(vk::Semaphore semaphore, vk::Fence fence, uint64_t timeout) {
    try {
        auto [result, imageIndex] = device_.acquireNextImageKHR(swapchain_, timeout, semaphore, fence);

        if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR) {
            needsRecreation_ = true;
            if (result == vk::Result::eErrorOutOfDateKHR) {
                return UINT32_MAX;
            }
        }

        return imageIndex;
    } catch (const vk::OutOfDateKHRError&) {
        needsRecreation_ = true;
        return UINT32_MAX;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Swapchain] Failed to acquire image: " << e.what() << std::endl;
        return UINT32_MAX;
    }
}

bool Swapchain::present(uint32_t imageIndex, const std::vector<vk::Semaphore>& waitSemaphores) {
    vk::PresentInfoKHR presentInfo{
        waitSemaphores,
        swapchain_,
        imageIndex
    };

    try {
        auto result = presentQueue_.presentKHR(presentInfo);
        if (result == vk::Result::eSuboptimalKHR) {
            needsRecreation_ = true;
        }
        return true;
    } catch (const vk::OutOfDateKHRError&) {
        needsRecreation_ = true;
        return false;
    } catch (const vk::SystemError& e) {
        std::cerr << "[Swapchain] Failed to present: " << e.what() << std::endl;
        return false;
    }
}

bool Swapchain::createFramebuffers(vk::RenderPass renderPass, vk::ImageView depthView) {
    for (auto& frame : frames_) {
        std::vector<vk::ImageView> attachments = {frame.imageView};
        if (depthView) {
            attachments.push_back(depthView);
        }

        vk::FramebufferCreateInfo framebufferInfo{
            {},
            renderPass,
            attachments,
            extent_.width,
            extent_.height,
            1
        };

        try {
            frame.framebuffer = device_.createFramebuffer(framebufferInfo);
        } catch (const vk::SystemError& e) {
            std::cerr << "[Swapchain] Failed to create framebuffer: " << e.what() << std::endl;
            return false;
        }
    }

    std::cout << "[Swapchain] Created " << frames_.size() << " framebuffers" << std::endl;
    return true;
}

void Swapchain::destroyFramebuffers() {
    for (auto& frame : frames_) {
        if (frame.framebuffer) {
            device_.destroyFramebuffer(frame.framebuffer);
            frame.framebuffer = nullptr;
        }
    }
}

vk::SurfaceFormatKHR Swapchain::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& formats,
                                                     const SwapchainConfig& config) {
    // Look for preferred format
    for (const auto& format : formats) {
        if (format.format == config.preferredFormat &&
            format.colorSpace == config.preferredColorSpace) {
            return format;
        }
    }

    // Fall back to first available
    return formats[0];
}

vk::PresentModeKHR Swapchain::choosePresentMode(const std::vector<vk::PresentModeKHR>& modes,
                                                 const SwapchainConfig& config) {
    // If vsync enabled, use FIFO (always available)
    if (config.vsync) {
        return vk::PresentModeKHR::eFifo;
    }

    // Look for preferred mode
    for (const auto& mode : modes) {
        if (mode == config.preferredPresentMode) {
            return mode;
        }
    }

    // Try mailbox for no vsync
    for (const auto& mode : modes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            return mode;
        }
    }

    // Try immediate
    for (const auto& mode : modes) {
        if (mode == vk::PresentModeKHR::eImmediate) {
            return mode;
        }
    }

    // Fall back to FIFO
    return vk::PresentModeKHR::eFifo;
}

vk::Extent2D Swapchain::chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities,
                                     uint32_t windowWidth, uint32_t windowHeight) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    vk::Extent2D extent = {windowWidth, windowHeight};
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    return extent;
}

void Swapchain::createImageViews() {
    for (auto& frame : frames_) {
        vk::ImageViewCreateInfo viewInfo{
            {},
            frame.image,
            vk::ImageViewType::e2D,
            format_,
            {},
            vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
        };

        try {
            frame.imageView = device_.createImageView(viewInfo);
        } catch (const vk::SystemError& e) {
            std::cerr << "[Swapchain] Failed to create image view: " << e.what() << std::endl;
        }
    }
}

void Swapchain::destroyImageViews() {
    for (auto& frame : frames_) {
        if (frame.imageView) {
            device_.destroyImageView(frame.imageView);
            frame.imageView = nullptr;
        }
    }
}

} // namespace window
