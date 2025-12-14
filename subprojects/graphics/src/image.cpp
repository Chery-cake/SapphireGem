#include "image.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <print>
#include <stdexcept>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Image::Image(const std::vector<LogicalDevice *> &devices,
             const ImageCreateInfo &createInfo)
    : identifier(createInfo.identifier), width(createInfo.width),
      height(createInfo.height), channels(createInfo.channels), mipLevels(1),
      format(createInfo.format), logicalDevices(devices) {

  deviceResources.reserve(logicalDevices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<ImageResources>());
  }

  // Calculate mip levels if needed
  if (createInfo.generateMipmaps && width > 0 && height > 0) {
    mipLevels = static_cast<uint32_t>(
                    std::floor(std::log2(std::max(width, height)))) +
                1;
  }
}

Image::~Image() {
  std::lock_guard lock(imageMutex);

  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    if (deviceResources[i]) {
      destroy_image(logicalDevices[i], *deviceResources[i]);
    }
  }
  deviceResources.clear();

  std::print("Image - {} - destructor executed\n", identifier);
}

bool Image::load_from_file(const std::string &filepath) {
  std::lock_guard lock(imageMutex);

  int w, h, c;
  unsigned char *data =
      stbi_load(filepath.c_str(), &w, &h, &c, STBI_rgb_alpha);

  if (!data) {
    std::print("Failed to load image: {}\n", filepath);
    return false;
  }

  width = static_cast<uint32_t>(w);
  height = static_cast<uint32_t>(h);
  channels = 4; // Force RGBA

  // Copy pixel data
  size_t dataSize = width * height * channels;
  pixelData.resize(dataSize);
  std::memcpy(pixelData.data(), data, dataSize);

  stbi_image_free(data);

  std::print("Image - {} - loaded from file: {} ({}x{}, {} channels)\n",
             identifier, filepath, width, height, channels);

  return true;
}

bool Image::load_from_memory(const unsigned char *data, uint32_t w,
                              uint32_t h, uint32_t c) {
  std::lock_guard lock(imageMutex);

  if (!data || w == 0 || h == 0 || c == 0) {
    std::print("Invalid image data\n");
    return false;
  }

  width = w;
  height = h;
  channels = c;

  size_t dataSize = width * height * channels;
  pixelData.resize(dataSize);
  std::memcpy(pixelData.data(), data, dataSize);

  std::print("Image - {} - loaded from memory ({}x{}, {} channels)\n",
             identifier, width, height, channels);

  return true;
}

void Image::apply_color_tint(const glm::vec4 &tint) {
  if (pixelData.empty()) {
    return;
  }

  for (size_t i = 0; i < pixelData.size(); i += channels) {
    if (channels >= 3) {
      pixelData[i + 0] =
          static_cast<unsigned char>(pixelData[i + 0] * tint.r);     // R
      pixelData[i + 1] =
          static_cast<unsigned char>(pixelData[i + 1] * tint.g);     // G
      pixelData[i + 2] =
          static_cast<unsigned char>(pixelData[i + 2] * tint.b);     // B
    }
    if (channels == 4) {
      pixelData[i + 3] =
          static_cast<unsigned char>(pixelData[i + 3] * tint.a);     // A
    }
  }
}

void Image::set_color_tint(const glm::vec4 &tint) {
  std::lock_guard lock(imageMutex);
  apply_color_tint(tint);
  std::print("Image - {} - applied color tint ({}, {}, {}, {})\n", identifier,
             tint.r, tint.g, tint.b, tint.a);
}

void Image::rotate_image_90(bool clockwise) {
  if (pixelData.empty() || width == 0 || height == 0) {
    return;
  }

  std::vector<unsigned char> rotated(pixelData.size());

  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      uint32_t srcIndex = (y * width + x) * channels;

      uint32_t newX, newY;
      if (clockwise) {
        newX = height - 1 - y;
        newY = x;
      } else {
        newX = y;
        newY = width - 1 - x;
      }

      uint32_t dstIndex = (newY * height + newX) * channels;

      for (uint32_t c = 0; c < channels; ++c) {
        rotated[dstIndex + c] = pixelData[srcIndex + c];
      }
    }
  }

  pixelData = std::move(rotated);
  std::swap(width, height);
}

void Image::rotate_90_clockwise() {
  std::lock_guard lock(imageMutex);
  rotate_image_90(true);
  std::print("Image - {} - rotated 90 degrees clockwise\n", identifier);
}

void Image::rotate_90_counter_clockwise() {
  std::lock_guard lock(imageMutex);
  rotate_image_90(false);
  std::print("Image - {} - rotated 90 degrees counter-clockwise\n",
             identifier);
}

void Image::rotate_180() {
  std::lock_guard lock(imageMutex);
  if (pixelData.empty()) {
    return;
  }

  size_t pixelCount = width * height;
  for (size_t i = 0; i < pixelCount / 2; ++i) {
    size_t j = pixelCount - 1 - i;
    for (uint32_t c = 0; c < channels; ++c) {
      std::swap(pixelData[i * channels + c], pixelData[j * channels + c]);
    }
  }
  std::print("Image - {} - rotated 180 degrees\n", identifier);
}

bool Image::create_image(LogicalDevice *device, ImageResources &resources,
                         const ImageCreateInfo &createInfo) {
  try {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = static_cast<VkFormat>(createInfo.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = static_cast<VkImageUsageFlags>(createInfo.usage);
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkResult result = vmaCreateImage(device->get_allocator(), &imageInfo,
                                     &allocInfo, &resources.image,
                                     &resources.allocation, nullptr);

    if (result != VK_SUCCESS) {
      std::print("Failed to create image\n");
      return false;
    }

    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create image: {}\n", e.what());
    return false;
  }
}

bool Image::create_image_view(LogicalDevice *device, ImageResources &resources,
                               const ImageCreateInfo &createInfo) {
  try {
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = resources.image;
    viewInfo.viewType = vk::ImageViewType::e2D;
    viewInfo.format = createInfo.format;
    viewInfo.subresourceRange.aspectMask = createInfo.aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    resources.imageView = device->get_device().createImageView(viewInfo);

    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create image view: {}\n", e.what());
    return false;
  }
}

bool Image::create_sampler(LogicalDevice *device, ImageResources &resources,
                            const ImageCreateInfo &createInfo) {
  try {
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = createInfo.filter;
    samplerInfo.minFilter = createInfo.filter;
    samplerInfo.addressModeU = createInfo.addressMode;
    samplerInfo.addressModeV = createInfo.addressMode;
    samplerInfo.addressModeW = createInfo.addressMode;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = 16.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);

    resources.sampler = device->get_device().createSampler(samplerInfo);

    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create sampler: {}\n", e.what());
    return false;
  }
}

bool Image::upload_data(LogicalDevice *device, ImageResources &resources,
                        const void *data, uint32_t dataSize) {
  try {
    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = dataSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    if (vmaCreateBuffer(device->get_allocator(), &bufferInfo, &allocInfo,
                        &stagingBuffer, &stagingAllocation,
                        nullptr) != VK_SUCCESS) {
      std::print("Failed to create staging buffer\n");
      return false;
    }

    // Copy data to staging buffer
    void *mappedData;
    vmaMapMemory(device->get_allocator(), stagingAllocation, &mappedData);
    std::memcpy(mappedData, data, dataSize);
    vmaUnmapMemory(device->get_allocator(), stagingAllocation);

    // Create command buffer for transfer
    vk::CommandBufferAllocateInfo allocateInfo{};
    allocateInfo.level = vk::CommandBufferLevel::ePrimary;
    allocateInfo.commandPool = *device->get_command_pool();
    allocateInfo.commandBufferCount = 1;

    auto commandBuffers =
        device->get_device().allocateCommandBuffers(allocateInfo);
    auto &commandBuffer = commandBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    commandBuffer.begin(beginInfo);

    // Transition image layout to transfer destination
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = resources.image;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                   vk::PipelineStageFlagBits::eTransfer, {}, {},
                                   {}, barrier);

    // Copy buffer to image
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    commandBuffer.copyBufferToImage(stagingBuffer, resources.image,
                                     vk::ImageLayout::eTransferDstOptimal,
                                     region);

    // Transition image layout to shader read-only
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                   vk::PipelineStageFlagBits::eFragmentShader,
                                   {}, {}, {}, barrier);

    commandBuffer.end();

    // Submit command buffer
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &*commandBuffer;

    device->get_graphics_queue().submit(submitInfo);
    device->get_graphics_queue().waitIdle();

    // Cleanup staging buffer
    vmaDestroyBuffer(device->get_allocator(), stagingBuffer,
                     stagingAllocation);

    return true;
  } catch (const std::exception &e) {
    std::print("Failed to upload image data: {}\n", e.what());
    return false;
  }
}

bool Image::update_gpu_data() {
  std::lock_guard lock(imageMutex);

  if (pixelData.empty()) {
    std::print("No pixel data to upload\n");
    return false;
  }

  ImageCreateInfo createInfo;
  createInfo.identifier = identifier;
  createInfo.width = width;
  createInfo.height = height;
  createInfo.channels = channels;
  createInfo.format = format;

  bool success = true;
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    auto &resources = deviceResources[i];

    // Destroy old image resources
    destroy_image(logicalDevices[i], *resources);

    // Create new image
    if (!create_image(logicalDevices[i], *resources, createInfo)) {
      success = false;
      continue;
    }

    // Create image view
    if (!create_image_view(logicalDevices[i], *resources, createInfo)) {
      success = false;
      continue;
    }

    // Create sampler
    if (!create_sampler(logicalDevices[i], *resources, createInfo)) {
      success = false;
      continue;
    }

    // Upload data
    if (!upload_data(logicalDevices[i], *resources, pixelData.data(),
                     static_cast<uint32_t>(pixelData.size()))) {
      success = false;
      continue;
    }
  }

  if (success) {
    std::print("Image - {} - updated GPU data\n", identifier);
  }

  return success;
}

void Image::destroy_image(LogicalDevice *device, ImageResources &resources) {
  resources.sampler.clear();
  resources.imageView.clear();

  if (resources.image != VK_NULL_HANDLE) {
    vmaDestroyImage(device->get_allocator(), resources.image,
                    resources.allocation);
    resources.image = VK_NULL_HANDLE;
    resources.allocation = VK_NULL_HANDLE;
  }

  resources.descriptorSets.clear();
}

const std::string &Image::get_identifier() const { return identifier; }

uint32_t Image::get_width() const { return width; }

uint32_t Image::get_height() const { return height; }

uint32_t Image::get_channels() const { return channels; }

vk::Format Image::get_format() const { return format; }

const std::vector<unsigned char> &Image::get_pixel_data() const {
  return pixelData;
}

VkImage Image::get_image(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->image;
}

vk::raii::ImageView &Image::get_image_view(uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->imageView;
}

vk::raii::Sampler &Image::get_sampler(uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->sampler;
}
