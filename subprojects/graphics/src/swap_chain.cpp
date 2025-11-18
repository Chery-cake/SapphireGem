#include "swap_chain.h"
#include "common.h"
#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <print>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

SwapChain::SwapChain(LogicalDevice *logicalDevice, GLFWwindow *window,
                     vk::raii::SurfaceKHR &surface)
    : logicalDevice(logicalDevice), window(window), surface(&surface),
      swapChain(nullptr), image(nullptr), imageView(nullptr) {}

SwapChain::SwapChain(LogicalDevice *logicalDevice, vk::SurfaceFormatKHR format,
                     vk::Extent2D extent2D)
    : logicalDevice(logicalDevice), window(nullptr), surface(nullptr),
      swapChain(nullptr), image(nullptr), imageView(nullptr),
      surfaceFormat(format), extent2D(extent2D) {}

SwapChain::~SwapChain() {

  clear_swap_chain();

  std::print(
      "Swap chain for device - {} - destructor executed\n",
      logicalDevice->get_physical_device()->get_properties().deviceName.data());
}

void SwapChain::set_surface_format(
    std::vector<vk::SurfaceFormatKHR> availableFormats) {
  assert(!availableFormats.empty());
  const auto iterator =
      std::ranges::find_if(availableFormats, [](const auto &format) {
        return format.format == vk::Format::eB8G8R8A8Srgb &&
               format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
      });
  surfaceFormat =
      iterator != availableFormats.end() ? *iterator : availableFormats[0];
}

void SwapChain::create_swap_chain() {

  if (surface != nullptr) {
    auto surfaceCapabilities = logicalDevice->get_physical_device()
                                   ->get_device()
                                   .getSurfaceCapabilitiesKHR(*surface);

    extent2D = [capabilities = &surfaceCapabilities,
                window = window]() -> vk::Extent2D {
      if (capabilities->currentExtent.width != 0xFFFFFFFF) {
        return capabilities->currentExtent;
      }
      int width = 0;
      int height = 0;
      glfwGetFramebufferSize(window, &width, &height);

      return {std::clamp<uint32_t>(width, capabilities->minImageExtent.width,
                                   capabilities->maxImageExtent.width),
              std::clamp<uint32_t>(height, capabilities->minImageExtent.height,
                                   capabilities->maxImageExtent.height)};
    }();

    set_surface_format(
        logicalDevice->get_physical_device()->get_device().getSurfaceFormatsKHR(
            *surface));

    auto swapChainPresentMode = [availablePresentModes =
                                     logicalDevice->get_physical_device()
                                         ->get_device()
                                         .getSurfacePresentModesKHR(
                                             *surface)]() {
      assert(std::ranges::any_of(availablePresentModes, [](auto presentMode) {
        return presentMode == vk::PresentModeKHR::eFifo;
      }));
      return std::ranges::any_of(availablePresentModes,
                                 [](const vk::PresentModeKHR value) {
                                   return vk::PresentModeKHR::eMailbox == value;
                                 })
                 ? vk::PresentModeKHR::eMailbox
                 : vk::PresentModeKHR::eFifo;
    }();

    vk::SwapchainCreateInfoKHR swapChainCreateInfo{
        .surface = *surface,
        .minImageCount = ((surfaceCapabilities.maxImageCount > 0) &&
                          (surfaceCapabilities.maxImageCount <
                           surfaceCapabilities.minImageCount))
                             ? surfaceCapabilities.maxImageCount
                             : surfaceCapabilities.minImageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent2D,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surfaceCapabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = swapChainPresentMode,
        .clipped = true};

    swapChain = vk::raii::SwapchainKHR(logicalDevice->get_device(),
                                       swapChainCreateInfo);
    swapChainImages = swapChain.getImages();

    std::print("Created Swap Chain for device: {}\n",
               logicalDevice->get_physical_device()
                   ->get_properties()
                   .deviceName.data());
    return;
  }

  vk::ImageCreateInfo imageInfo{
      .imageType = vk::ImageType::e2D,
      .format = surfaceFormat.format,
      .extent = vk::Extent3D(extent2D.width, extent2D.height, 1),
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = vk::ImageTiling::eOptimal,
      .usage = vk::ImageUsageFlagBits::eColorAttachment |
               vk::ImageUsageFlagBits::eSampled |
               vk::ImageUsageFlagBits::eTransferSrc,
      .sharingMode = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined};

  image = logicalDevice->get_device().createImage(imageInfo);

  vk::ImageViewCreateInfo viewInfo{
      .image = *image,
      .viewType = vk::ImageViewType::e2D,
      .format = surfaceFormat.format,
      .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  imageView = logicalDevice->get_device().createImageView(viewInfo);

  std::print(
      "Created Swap Image for device: {}\n",
      logicalDevice->get_physical_device()->get_properties().deviceName.data());
}

void SwapChain::clear_swap_chain() {
  if (surface != nullptr) {
    swapChainImageViews.clear();
    swapChainImages.clear();
    swapChain.clear();
    return;
  }

  imageView.clear();
  image.clear();
}

void SwapChain::create_swap_image_views() {
  swapChainImageViews.clear();

  vk::ImageViewCreateInfo imageViewCreateInfo{
      .viewType = vk::ImageViewType::e2D,
      .format = surfaceFormat.format,
      .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
  for (auto image : swapChainImages) {
    imageViewCreateInfo.image = image;
    swapChainImageViews.emplace_back(logicalDevice->get_device(),
                                     imageViewCreateInfo);
  }
}

void SwapChain::recreate_swap_chain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);
  while (width == 0 || height == 0) {
    glfwGetFramebufferSize(window, &width, &height);
    glfwWaitEvents();
  }

  clear_swap_chain();

  create_swap_chain();

  create_swap_image_views();
}

void SwapChain::recreate_swap_chain(vk::SurfaceFormatKHR format,
                                    vk::Extent2D extent) {
  surfaceFormat = format;
  extent2D = extent;

  clear_swap_chain();
  create_swap_chain();
}

vk::SurfaceFormatKHR SwapChain::get_surface_format() { return surfaceFormat; }

vk::Extent2D SwapChain::get_extent2D() { return extent2D; }
