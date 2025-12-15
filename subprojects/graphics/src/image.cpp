#include "image.h"
#include <print>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

render::Image::Image(const std::vector<device::LogicalDevice *> &devices,
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
    mipLevels =
        static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) +
        1;
  }
}

render::Image::~Image() {
  std::lock_guard lock(imageMutex);

  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    if (deviceResources[i]) {
      destroy_image(logicalDevices[i], *deviceResources[i]);
    }
  }
  deviceResources.clear();

  std::print("Image - {} - destructor executed\n", identifier);
}

bool render::Image::create_image(device::LogicalDevice *device,
                                 ImageResources &resources,
                                 const ImageCreateInfo &createInfo) {
  try {
    VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = static_cast<VkFormat>(createInfo.format),
        .extent = {width, height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = static_cast<VkImageUsageFlags>(createInfo.usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_GPU_ONLY};

    VkResult result =
        vmaCreateImage(device->get_allocator(), &imageInfo, &allocInfo,
                       &resources.image, &resources.allocation, nullptr);

    if (result != VK_SUCCESS) {
      std::print(stderr, "Failed to create image\n");
      return false;
    }

    return true;
  } catch (const std::exception &e) {
    std::print(stderr, "Failed to create image: {}\n", e.what());
    return false;
  }
}

bool render::Image::create_image_view(device::LogicalDevice *device,
                                      ImageResources &resources,
                                      const ImageCreateInfo &createInfo) {
  try {
    vk::ImageViewCreateInfo viewInfo{
        .image = resources.image,
        .viewType = vk::ImageViewType::e2D,
        .format = createInfo.format,
        .subresourceRange = {createInfo.aspect, 0, mipLevels, 0, 1}};

    resources.imageView = device->get_device().createImageView(viewInfo);

    return true;
  } catch (const std::exception &e) {
    std::print(stderr, "Failed to create image view: {}\n", e.what());
    return false;
  }
}

bool render::Image::create_sampler(device::LogicalDevice *device,
                                   ImageResources &resources,
                                   const ImageCreateInfo &createInfo) {
  try {
    vk::SamplerCreateInfo samplerInfo{
        .magFilter = createInfo.filter,
        .minFilter = createInfo.filter,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = createInfo.addressMode,
        .addressModeV = createInfo.addressMode,
        .addressModeW = createInfo.addressMode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .compareOp = vk::CompareOp::eAlways,
        .minLod = 0.0f,
        .maxLod = static_cast<float>(mipLevels),
        .borderColor = vk::BorderColor::eIntOpaqueBlack,
        .unnormalizedCoordinates = VK_FALSE,
    };

    resources.sampler = device->get_device().createSampler(samplerInfo);

    return true;
  } catch (const std::exception &e) {
    std::print(stderr, "Failed to create sampler: {}\n", e.what());
    return false;
  }
}

bool render::Image::upload_data(device::LogicalDevice *device,
                                ImageResources &resources, const void *data,
                                uint32_t dataSize) {
  try {
    // Create staging buffer
    VkBufferCreateInfo bufferInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = dataSize,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    VmaAllocationCreateInfo allocInfo{.usage = VMA_MEMORY_USAGE_CPU_ONLY};

    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;

    if (vmaCreateBuffer(device->get_allocator(), &bufferInfo, &allocInfo,
                        &stagingBuffer, &stagingAllocation,
                        nullptr) != VK_SUCCESS) {
      std::print(stderr, "Failed to create staging buffer\n");
      return false;
    }

    // Copy data to staging buffer
    void *mappedData;
    vmaMapMemory(device->get_allocator(), stagingAllocation, &mappedData);
    std::memcpy(mappedData, data, dataSize);
    vmaUnmapMemory(device->get_allocator(), stagingAllocation);

    // Create command buffer for transfer
    vk::CommandBufferAllocateInfo allocateInfo{
        .commandPool = *device->get_command_pool(),
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1};

    auto commandBuffers =
        device->get_device().allocateCommandBuffers(allocateInfo);
    auto &commandBuffer = commandBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    commandBuffer.begin(beginInfo);

    // Transition image layout to transfer destination
    vk::ImageMemoryBarrier barrier{
        .srcAccessMask = {},
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eUndefined,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = resources.image,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0,
                             1}};

    commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                  vk::PipelineStageFlagBits::eTransfer, {}, {},
                                  {}, barrier);

    // Copy buffer to image
    vk::BufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .imageOffset = vk::Offset3D{0, 0, 0},
        .imageExtent = vk::Extent3D{width, height, 1}};

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
    vk::SubmitInfo submitInfo{.commandBufferCount = 1,
                              .pCommandBuffers = &*commandBuffer};

    device->get_graphics_queue().submit(submitInfo);
    device->get_graphics_queue().waitIdle();

    // Cleanup staging buffer
    vmaDestroyBuffer(device->get_allocator(), stagingBuffer, stagingAllocation);

    return true;
  } catch (const std::exception &e) {
    std::print(stderr, "Failed to upload image data: {}\n", e.what());
    return false;
  }
}

void render::Image::destroy_image(device::LogicalDevice *device,
                                  ImageResources &resources) {
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

void render::Image::apply_color_tint(const glm::vec4 &tint) {
  if (pixelData.empty()) {
    return;
  }

  for (size_t i = 0; i < pixelData.size(); i += channels) {
    if (channels >= 3) {
      pixelData[i + 0] =
          static_cast<unsigned char>(pixelData[i + 0] * tint.r); // R
      pixelData[i + 1] =
          static_cast<unsigned char>(pixelData[i + 1] * tint.g); // G
      pixelData[i + 2] =
          static_cast<unsigned char>(pixelData[i + 2] * tint.b); // B
    }
    if (channels == 4) {
      pixelData[i + 3] =
          static_cast<unsigned char>(pixelData[i + 3] * tint.a); // A
    }
  }
}

void render::Image::rotate_image_90(bool clockwise) {
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

bool render::Image::load_from_file(const std::string &filepath) {
  std::lock_guard lock(imageMutex);

  int w, h, c;
  unsigned char *data = stbi_load(filepath.c_str(), &w, &h, &c, STBI_rgb_alpha);

  if (!data) {
    std::print(stderr, "Failed to load image: {}\n", filepath);
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

bool render::Image::load_from_memory(const unsigned char *data, uint32_t w,
                                     uint32_t h, uint32_t c) {
  std::lock_guard lock(imageMutex);

  if (!data || w == 0 || h == 0 || c == 0) {
    std::print(stderr, "Invalid image data\n");
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

void render::Image::set_color_tint(const glm::vec4 &tint) {
  std::lock_guard lock(imageMutex);
  apply_color_tint(tint);
  std::print("Image - {} - applied color tint ({}, {}, {}, {})\n", identifier,
             tint.r, tint.g, tint.b, tint.a);
}

void render::Image::rotate_90_clockwise() {
  std::lock_guard lock(imageMutex);
  rotate_image_90(true);
  std::print("Image - {} - rotated 90 degrees clockwise\n", identifier);
}

void render::Image::rotate_90_counter_clockwise() {
  std::lock_guard lock(imageMutex);
  rotate_image_90(false);
  std::print("Image - {} - rotated 90 degrees counter-clockwise\n", identifier);
}

void render::Image::rotate_180() {
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

bool render::Image::update_gpu_data() {
  std::lock_guard lock(imageMutex);

  if (pixelData.empty()) {
    std::print(stderr, "No pixel data to upload\n");
    return false;
  }

  ImageCreateInfo createInfo = {.identifier = identifier,
                                .width = width,
                                .height = height,
                                .channels = channels,
                                .format = format};

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

const std::string &render::Image::get_identifier() const { return identifier; }

uint32_t render::Image::get_width() const { return width; }

uint32_t render::Image::get_height() const { return height; }

uint32_t render::Image::get_channels() const { return channels; }

vk::Format render::Image::get_format() const { return format; }

const std::vector<unsigned char> &render::Image::get_pixel_data() const {
  return pixelData;
}

VkImage render::Image::get_image(uint32_t deviceIndex) const {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->image;
}

vk::raii::ImageView &render::Image::get_image_view(uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->imageView;
}

vk::raii::Sampler &render::Image::get_sampler(uint32_t deviceIndex) {
  if (deviceIndex >= deviceResources.size()) {
    throw std::out_of_range("Device index out of range");
  }
  return deviceResources[deviceIndex]->sampler;
}
