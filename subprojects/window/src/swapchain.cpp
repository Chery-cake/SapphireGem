#include "swapchain.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include <cstdio>
#include <memory>
#include <mutex>
#include <print>
#include <vector>

namespace window {

Swapchain::Swapchain() = default;

Swapchain::~Swapchain() { destroy(); }

Swapchain::Swapchain(Swapchain &&other) noexcept
    : mainGPUDevice_(other.mainGPUDevice_),
      secondaryGPUDevices_(std::move(other.secondaryGPUDevices_)),
      surface_(std::move(other.surface_)),
      presentQueue_(std::move(other.presentQueue_)),
      presentQueueFamily_(other.presentQueueFamily_),
      swapchain_(std::move(other.swapchain_)), format_(other.format_),
      extent_(other.extent_), frames_(std::move(other.frames_)),
      config_(other.config_), needsRecreation_(other.needsRecreation_) {
  other.mainGPUDevice_ = nullptr;
  other.secondaryGPUDevices_.clear();
}

Swapchain &Swapchain::operator=(Swapchain &&other) noexcept {
  if (this != &other) {
    destroy();
    mainGPUDevice_ = other.mainGPUDevice_;
    secondaryGPUDevices_ = std::move(other.secondaryGPUDevices_);
    surface_ = std::move(other.surface_);
    presentQueue_ = std::move(other.presentQueue_);
    presentQueueFamily_ = other.presentQueueFamily_;
    swapchain_ = std::move(other.swapchain_);
    format_ = other.format_;
    extent_ = other.extent_;
    frames_ = std::move(other.frames_);
    config_ = other.config_;
    needsRecreation_ = other.needsRecreation_;
    other.mainGPUDevice_ = nullptr;
    other.secondaryGPUDevices_.clear();
  }
  return *this;
}

void Swapchain::destroy() {
  if (!mainGPUDevice_) {
    return;
  }

  std::lock_guard<std::mutex> lock(swapchainMutex_);

  mainGPUDevice_->waitIdle();
  for (const auto &gpu : secondaryGPUDevices_) {
    gpu->waitIdle();
  }

  // RAII handles cleanup - destroy framebuffers and image views
  destroyFramebuffers();
  destroyImageViews();

  // RAII handles swapchain destruction
  offscreenImages_.clear();
  swapchain_.reset();

  frames_.clear();
  mainGPUDevice_ = nullptr;
  secondaryGPUDevices_.clear();
  std::println("[Swapchain] Destroyed");
}

bool Swapchain::create(device::GPUDevice *device,
                       std::vector<device::GPUDevice *> &secondary,
                       vk::raii::SurfaceKHR &surface,
                       const SwapchainConfig &config) {
  if (swapchain_) {
    std::println(stderr, "[Swapchain] Already created");
    return false;
  }

  mainGPUDevice_ = device;
  secondaryGPUDevices_ = secondary;

  surface_ = std::make_unique<vk::raii::SurfaceKHR>(std::move(surface));
  presentQueue_ = device->getPresentQueue();
  presentQueueFamily_ = device->getQueueFamilies().presentFamily.value_or(0);
  config_ = config;

  vk::PhysicalDevice physicalDevice = device->getPhysicalDevice();

  // Query swapchain support
  auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(**surface_);
  auto formats = physicalDevice.getSurfaceFormatsKHR(**surface_);
  auto presentModes = physicalDevice.getSurfacePresentModesKHR(**surface_);

  if (formats.empty() || presentModes.empty()) {
    std::println(stderr, "[Swapchain] Inadequate swapchain support");
    return false;
  }

  // Choose format, present mode, and extent
  auto surfaceFormat = chooseSurfaceFormat(formats, config);
  auto presentMode = choosePresentMode(presentModes, config);
  extent_ = chooseExtent(capabilities, capabilities.currentExtent.width,
                         capabilities.currentExtent.height);
  format_ = surfaceFormat.format;

  // Determine image count
  uint32_t imageCount =
      std::max(config.minImageCount, capabilities.minImageCount);
  if (capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }

  // Create swapchain
  vk::SwapchainCreateInfoKHR createInfo{
      {},
      *surface_,
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
      nullptr // Old swapchain
  };

  // Handle multiple queue families
  uint32_t graphicsFamily =
      device->getQueueFamilies().graphicsFamily.value_or(0);
  std::array<uint32_t, 2> queueFamilyIndices = {graphicsFamily,
                                                presentQueueFamily_};
  if (graphicsFamily != presentQueueFamily_) {
    createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
  }

  try {
    swapchain_ = std::make_unique<vk::raii::SwapchainKHR>(
        device->getRaiiDevice(), createInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Swapchain] Failed to create swapchain: {}",
                 e.what());
    return false;
  }

  // Get swapchain images and create views
  auto images = swapchain_->getImages();
  frames_.resize(images.size());
  for (size_t i = 0; i < images.size(); ++i) {
    frames_[i].image = images[i];
    frames_[i].index = static_cast<uint32_t>(i);
  }

  createImageViews();
  needsRecreation_ = false;

  std::println("[Swapchain] Created: {}x{} ({} images)", extent_.width,
               extent_.height, frames_.size());
  return true;
}

bool Swapchain::recreate(uint32_t newWidth, uint32_t newHeight) {
  if (newWidth == 0 || newHeight == 0) {
    return false; // Window minimized
  }

  if (!mainGPUDevice_) {
    return false;
  }

  std::lock_guard<std::mutex> lock(swapchainMutex_);

  mainGPUDevice_->waitIdle();
  for (const auto &gpu : secondaryGPUDevices_) {
    gpu->waitIdle();
  }

  vk::PhysicalDevice physicalDevice = mainGPUDevice_->getPhysicalDevice();

  // Store old swapchain for handoff
  vk::SwapchainKHR oldSwapchainHandle =
      swapchain_ ? **swapchain_ : vk::SwapchainKHR{};

  // Query updated capabilities
  auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(**surface_);
  auto formats = physicalDevice.getSurfaceFormatsKHR(**surface_);
  auto presentModes = physicalDevice.getSurfacePresentModesKHR(**surface_);

  auto surfaceFormat = chooseSurfaceFormat(formats, config_);
  auto presentMode = choosePresentMode(presentModes, config_);
  extent_ = chooseExtent(capabilities, newWidth, newHeight);
  format_ = surfaceFormat.format;

  uint32_t imageCount =
      std::max(config_.minImageCount, capabilities.minImageCount);
  if (capabilities.maxImageCount > 0) {
    imageCount = std::min(imageCount, capabilities.maxImageCount);
  }

  vk::SwapchainCreateInfoKHR createInfo{
      {},
      *surface_,
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
      oldSwapchainHandle};

  // Destroy old image views first
  destroyImageViews();

  try {
    auto newSwapchain = std::make_unique<vk::raii::SwapchainKHR>(
        mainGPUDevice_->getRaiiDevice(), createInfo);
    // Old swapchain will be destroyed when we reset
    swapchain_ = std::move(newSwapchain);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Swapchain] Failed to recreate swapchain: {}",
                 e.what());
    return false;
  }

  // Get new images
  auto images = swapchain_->getImages();
  frames_.resize(images.size());
  for (size_t i = 0; i < images.size(); ++i) {
    frames_[i].image = images[i];
    frames_[i].index = static_cast<uint32_t>(i);
    frames_[i].framebuffer.reset(); // Needs recreation
  }

  createImageViews();
  needsRecreation_ = false;

  std::println("[Swapchain] Recreated: {}x{}", extent_.width, extent_.height);
  return true;
}

uint32_t Swapchain::acquireNextImage(vk::Semaphore semaphore, vk::Fence fence,
                                     uint64_t timeout) {
  if (!swapchain_) {
    return UINT32_MAX;
  }

  try {
    auto [result, imageIndex] =
        swapchain_->acquireNextImage(timeout, semaphore, fence);

    if (result == vk::Result::eErrorOutOfDateKHR ||
        result == vk::Result::eSuboptimalKHR) {
      needsRecreation_ = true;
      if (result == vk::Result::eErrorOutOfDateKHR) {
        return UINT32_MAX;
      }
    }

    return imageIndex;
  } catch (const vk::OutOfDateKHRError &) {
    needsRecreation_ = true;
    return UINT32_MAX;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Swapchain] Failed to acquire image: {}", e.what());
    return UINT32_MAX;
  }
}

bool Swapchain::present(uint32_t imageIndex,
                        const std::vector<vk::Semaphore> &waitSemaphores) {
  if (!swapchain_) {
    return false;
  }

  vk::SwapchainKHR swapchainHandle = **swapchain_;
  vk::PresentInfoKHR presentInfo{waitSemaphores, swapchainHandle, imageIndex};

  try {
    auto result = presentQueue_.presentKHR(presentInfo);
    if (result == vk::Result::eSuboptimalKHR) {
      needsRecreation_ = true;
    }
    return true;
  } catch (const vk::OutOfDateKHRError &) {
    needsRecreation_ = true;
    return false;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Swapchain] Failed to present: {}", e.what());
    return false;
  }
}

bool Swapchain::createFramebuffers(vk::RenderPass renderPass,
                                   vk::ImageView depthView) {
  if (!mainGPUDevice_) {
    return false;
  }

  for (auto &frame : frames_) {
    std::vector<vk::ImageView> attachments;
    if (frame.imageView) {
      attachments.push_back(*frame.imageView);
    }
    if (depthView) {
      attachments.push_back(depthView);
    }

    vk::FramebufferCreateInfo framebufferInfo{
        {}, renderPass, attachments, extent_.width, extent_.height, 1};

    try {
      frame.framebuffer = std::make_unique<vk::raii::Framebuffer>(
          mainGPUDevice_->getRaiiDevice(), framebufferInfo);
    } catch (const vk::SystemError &e) {
      std::println(stderr, "[Swapchain] Failed to create framebuffer: {}",
                   e.what());
      return false;
    }
  }

  std::println("[Swapchain] Created {} framebuffers", frames_.size());
  return true;
}

void Swapchain::destroyFramebuffers() {
  for (auto &frame : frames_) {
    frame.framebuffer.reset(); // RAII handles destruction
  }
}

vk::SurfaceFormatKHR
Swapchain::chooseSurfaceFormat(const std::vector<vk::SurfaceFormatKHR> &formats,
                               const SwapchainConfig &config) {
  // Look for preferred format
  for (const auto &format : formats) {
    if (format.format == config.preferredFormat &&
        format.colorSpace == config.preferredColorSpace) {
      return format;
    }
  }

  // Fall back to first available
  return formats[0];
}

vk::PresentModeKHR
Swapchain::choosePresentMode(const std::vector<vk::PresentModeKHR> &modes,
                             const SwapchainConfig &config) {
  // If vsync enabled, use FIFO (always available)
  if (config.vsync) {
    return vk::PresentModeKHR::eFifo;
  }

  // Look for preferred mode
  for (const auto &mode : modes) {
    if (mode == config.preferredPresentMode) {
      return mode;
    }
  }

  // Try mailbox for no vsync
  for (const auto &mode : modes) {
    if (mode == vk::PresentModeKHR::eMailbox) {
      return mode;
    }
  }

  // Try immediate
  for (const auto &mode : modes) {
    if (mode == vk::PresentModeKHR::eImmediate) {
      return mode;
    }
  }

  // Fall back to FIFO
  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D
Swapchain::chooseExtent(const vk::SurfaceCapabilitiesKHR &capabilities,
                        uint32_t windowWidth, uint32_t windowHeight) {
  if (capabilities.currentExtent.width != UINT32_MAX) {
    return capabilities.currentExtent;
  }

  vk::Extent2D extent = {windowWidth, windowHeight};
  extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                            capabilities.maxImageExtent.width);
  extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                             capabilities.maxImageExtent.height);
  return extent;
}

void Swapchain::createImageViews() {
  if (!mainGPUDevice_) {
    return;
  }

  for (auto &frame : frames_) {
    vk::ImageViewCreateInfo viewInfo{
        {},
        frame.image,
        vk::ImageViewType::e2D,
        format_,
        {},
        vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

    try {
      frame.imageView = std::make_unique<vk::raii::ImageView>(
          mainGPUDevice_->getRaiiDevice(), viewInfo);
    } catch (const vk::SystemError &e) {
      std::println(stderr, "[Swapchain] Failed to create image view: {}",
                   e.what());
    }
  }
}

void Swapchain::destroyImageViews() {
  for (auto &frame : frames_) {
    frame.imageView.reset(); // RAII handles destruction
  }
}

} // namespace window
