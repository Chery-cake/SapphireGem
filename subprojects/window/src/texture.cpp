#include "texture.h"
#include "SDL3/SDL_surface.h"
#include "SDL3_image/SDL_image.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <print>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

namespace window {

Texture::Texture(const TextureTag &tag) : name_(tag.name) {
  layers_.reserve(tag.layerCount > 0 ? tag.layerCount : 1);
  // Auto-populate layers from tag's embedded layer info
  if (tag.layers && tag.layerCount > 0) {
    for (uint32_t i = 0; i < tag.layerCount; ++i) {
      TextureLayer layer;
      layer.imageTag = tag.layers[i].imageTag;
      layer.transform = tag.layers[i].defaultTransform;
      layers_.push_back(std::move(layer));
    }
  }
}

Texture::~Texture() { release(); }

Texture::Texture(Texture &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.textureMutex_);
  name_ = std::move(other.name_);
  layers_ = std::move(other.layers_);
  sampler_ = std::move(other.sampler_);
  uploaded_ = other.uploaded_;
  other.uploaded_ = false;
}

Texture &Texture::operator=(Texture &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(textureMutex_, other.textureMutex_);
    release();
    name_ = std::move(other.name_);
    layers_ = std::move(other.layers_);
    sampler_ = std::move(other.sampler_);
    uploaded_ = other.uploaded_;
    other.uploaded_ = false;
  }
  return *this;
}

uint32_t Texture::addLayer(const ImageTag *imageTag,
                           const ImageTransform &transform) {
  std::lock_guard<std::mutex> lock(textureMutex_);

  TextureLayer layer;
  layer.imageTag = imageTag;
  layer.transform = transform;
  layers_.push_back(std::move(layer));

  return static_cast<uint32_t>(layers_.size() - 1);
}

bool Texture::setLayerTransform(uint32_t layerIndex,
                                const ImageTransform &transform) {
  std::lock_guard<std::mutex> lock(textureMutex_);
  if (layerIndex >= layers_.size()) {
    return false;
  }
  layers_[layerIndex].transform = transform;
  return true;
}

const ImageTransform *Texture::getLayerTransform(uint32_t layerIndex) const {
  std::lock_guard<std::mutex> lock(textureMutex_);
  if (layerIndex >= layers_.size()) {
    return nullptr;
  }
  return &layers_[layerIndex].transform;
}

bool Texture::upload(device::VMAAllocator &allocator,
                     device::GPUDevice &device) {
  std::lock_guard<std::mutex> lock(textureMutex_);

  if (uploaded_) {
    return true;
  }

  if (!device.getQueueFamilies().hasGraphics()) {
    std::println(stderr,
                 "[Texture] No graphics queue family available for upload");
    return false;
  }
  uint32_t graphicsFamily = device.getQueueFamilies().graphicsFamily.value();

  // Pass 1: Load surfaces, create images & staging buffers
  std::unordered_map<std::string, SDL_Surface *> atlasCache;
  auto cleanupCache = [&atlasCache]() {
    std::ranges::for_each(
        atlasCache, [](const auto &pair) { SDL_DestroySurface(pair.second); });
    atlasCache.clear();
  };

  // We'll keep all staging buffers alive until after the single submission.
  std::vector<device::AllocatedBuffer> stagingBuffers;
  stagingBuffers.reserve(layers_.size());

  for (TextureLayer &layer : layers_) {
    if (!layer.imageTag) {
      std::println(stderr, "[Texture] Layer has no image tag in texture: {}",
                   name_);
      cleanupCache();
      return false;
    }

    const char *filePath = nullptr;
    bool isAtlas = false;
    uint32_t srcX = 0, srcY = 0, regionW = 0, regionH = 0;
    uint32_t fileW = 0, fileH = 0;
    const char *layerName = layer.imageTag->getName();

    std::visit(
        [&](const auto &src) {
          using T = std::decay_t<decltype(src)>;
          if constexpr (std::is_same_v<T, ImageFromFile>) {
            filePath = src.path;
            fileW = src.width;
            fileH = src.height;
            isAtlas = false;
          } else if constexpr (std::is_same_v<T, ImageFromAtlasRegion>) {
            if (src.atlas) {
              filePath = src.atlas->path;
            }
            srcX = src.x;
            srcY = src.y;
            regionW = src.width;
            regionH = src.height;
            isAtlas = true;
          }
        },
        layer.imageTag->source);

    if (!filePath) {
      std::println(stderr,
                   "[Texture] No file path for image '{}' in texture: {}",
                   layerName, name_);
      cleanupCache();
      return false;
    }

    // Load or reuse surface
    SDL_Surface *rgbaSurface = nullptr;
    bool ownsSurface = false;

    if (isAtlas) {
      auto it = atlasCache.find(std::string(filePath));
      if (it != atlasCache.end()) {
        rgbaSurface = it->second;
      } else {
        SDL_Surface *loaded = IMG_Load(filePath);
        if (!loaded) {
          std::println(stderr, "[Texture] Failed to load atlas '{}': {}",
                       filePath, SDL_GetError());
          cleanupCache();
          return false;
        }
        if (loaded->format != SDL_PIXELFORMAT_RGBA32) {
          rgbaSurface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
          SDL_DestroySurface(loaded);
          if (!rgbaSurface) {
            std::println(stderr,
                         "[Texture] Failed to convert atlas '{}' "
                         "to RGBA: {}",
                         filePath, SDL_GetError());
            cleanupCache();
            return false;
          }
        } else {
          rgbaSurface = loaded;
        }
        atlasCache[std::string(filePath)] = rgbaSurface;
      }
    } else {
      SDL_Surface *loaded = IMG_Load(filePath);
      if (!loaded) {
        std::println(stderr, "[Texture] Failed to load image '{}': {}",
                     filePath, SDL_GetError());
        cleanupCache();
        return false;
      }
      if (loaded->format != SDL_PIXELFORMAT_RGBA32) {
        rgbaSurface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loaded);
        if (!rgbaSurface) {
          std::println(stderr,
                       "[Texture] Failed to convert image '{}' to RGBA: {}",
                       filePath, SDL_GetError());
          cleanupCache();
          return false;
        }
      } else {
        rgbaSurface = loaded;
      }
      ownsSurface = true;
    }

    // Determine final dimensions
    uint32_t width, height;
    uint32_t surfW = static_cast<uint32_t>(rgbaSurface->w);
    uint32_t surfH = static_cast<uint32_t>(rgbaSurface->h);

    if (isAtlas) {
      width = regionW > 0 ? regionW : surfW;
      height = regionH > 0 ? regionH : surfH;
      if (srcX + width > surfW || srcY + height > surfH) {
        std::println(stderr,
                     "[Texture] Atlas region exceeds surface dimensions");
        if (ownsSurface)
          SDL_DestroySurface(rgbaSurface);
        cleanupCache();
        return false;
      }
    } else {
      width = fileW > 0 ? fileW : surfW;
      height = fileH > 0 ? fileH : surfH;
      if (width > surfW)
        width = surfW;
      if (height > surfH)
        height = surfH;
    }

    constexpr uint32_t bytesPerPixel = 4;
    vk::DeviceSize imageSize = width * height * bytesPerPixel;

    // Create staging buffer for this layer
    auto staging = allocator.createStagingBuffer(
        imageSize, std::string(layerName) + "_staging");
    if (!staging.isValid()) {
      std::println(stderr, "[Texture] Failed to create staging buffer for: {}",
                   layerName);
      if (ownsSurface)
        SDL_DestroySurface(rgbaSurface);
      cleanupCache();
      return false;
    }

    // Copy pixels to staging buffer
    {
      void *mapped = staging.map();
      if (!mapped) {
        std::println(stderr, "[Texture] Failed to map staging buffer for: {}",
                     layerName);
        if (ownsSurface)
          SDL_DestroySurface(rgbaSurface);
        cleanupCache();
        return false;
      }

      const auto *srcPixels = static_cast<const uint8_t *>(rgbaSurface->pixels);
      auto *dstPixels = static_cast<uint8_t *>(mapped);
      size_t srcPitch = rgbaSurface->pitch;
      uint32_t dstRowSize = width * bytesPerPixel;

      std::ranges::for_each(std::views::iota(0u, height), [&](uint32_t row) {
        const uint8_t *srcRow =
            srcPixels + (srcY + row) * srcPitch + srcX * bytesPerPixel;
        uint8_t *dstRow = dstPixels + row * dstRowSize;
        std::memcpy(dstRow, srcRow, dstRowSize);
      });

      staging.flush();
      staging.unmap();
    }

    if (ownsSurface)
      SDL_DestroySurface(rgbaSurface);

    // Create GPU image
    layer.gpuImage = allocator.createImage2D(
        width, height, vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        std::string(layerName) + "_image");
    if (!layer.gpuImage.isValid()) {
      std::println(stderr, "[Texture] Failed to create GPU image for layer: {}",
                   layerName);
      cleanupCache();
      return false;
    }

    if (!allocator.createImageView(layer.gpuImage)) {
      std::println(stderr,
                   "[Texture] Failed to create image view for layer: {}",
                   layerName);
      cleanupCache();
      return false;
    }

    // Save staging buffer for later
    stagingBuffers.push_back(std::move(staging));
    layer.loaded = true; // data ready, transfer still pending
  }

  // All surface data has been copied into staging buffers — free atlas
  // surfaces
  cleanupCache();

  // Pass 2: Single command buffer records all copies & barriers
  {
    vk::CommandPoolCreateInfo poolInfo{
        vk::CommandPoolCreateFlagBits::eTransient, graphicsFamily};
    vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);

    vk::CommandBufferAllocateInfo allocInfo{
        *cmdPool, vk::CommandBufferLevel::ePrimary, 1};
    vk::raii::CommandBuffers cmdBuffers(device.getRaiiDevice(), allocInfo);
    auto &cmd = cmdBuffers[0];

    vk::CommandBufferBeginInfo beginInfo{
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    cmd.begin(beginInfo);

    std::ranges::for_each(
        std::views::zip(layers_, stagingBuffers), [&](const auto &tuple) {
          auto &[layer, staging] = tuple;

          // Transition: UNDEFINED → TRANSFER_DST_OPTIMAL
          {
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eUndefined;
            barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = layer.gpuImage.getImage();
            barrier.subresourceRange.aspectMask =
                vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = {};
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                                vk::PipelineStageFlagBits::eTransfer, {}, {},
                                {}, barrier);
          }

          // Copy staging buffer → image
          vk::BufferImageCopy copyRegion{};
          copyRegion.bufferOffset = 0;
          copyRegion.bufferRowLength = 0; // tightly packed
          copyRegion.bufferImageHeight = 0;
          copyRegion.imageSubresource.aspectMask =
              vk::ImageAspectFlagBits::eColor;
          copyRegion.imageSubresource.mipLevel = 0;
          copyRegion.imageSubresource.baseArrayLayer = 0;
          copyRegion.imageSubresource.layerCount = 1;
          copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
          copyRegion.imageExtent = vk::Extent3D{
              layer.gpuImage.extent.width, layer.gpuImage.extent.height, 1};

          cmd.copyBufferToImage(staging.getBuffer(), layer.gpuImage.getImage(),
                                vk::ImageLayout::eTransferDstOptimal,
                                copyRegion);

          // Transition: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
          {
            vk::ImageMemoryBarrier barrier{};
            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = layer.gpuImage.getImage();
            barrier.subresourceRange.aspectMask =
                vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

            cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                vk::PipelineStageFlagBits::eFragmentShader, {},
                                {}, {}, barrier);
          }
        });
    cmd.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.setCommandBuffers(*cmd);
    device.getGraphicsQueue().submit(submitInfo);
    device.getGraphicsQueue().waitIdle();
  }

  // Verify all layers uploaded successfully
  for (const auto &layer : layers_) {
    if (!layer.loaded) {
      std::println(stderr, "[Texture] Not all layers uploaded for texture: {}",
                   name_);
      return false;
    }
  }

  uploaded_ = true;
  return true;
}

void Texture::release() {
  std::lock_guard<std::mutex> lock(textureMutex_);
  sampler_.reset();
  layers_.clear();
  uploaded_ = false;
}

bool Texture::createSampler(device::GPUDevice &device) {
  std::lock_guard<std::mutex> lock(textureMutex_);

  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.anisotropyEnable = vk::False;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = vk::False;
  samplerInfo.compareEnable = vk::False;
  samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

  try {
    sampler_ = std::make_unique<vk::raii::Sampler>(device.getRaiiDevice(),
                                                   samplerInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Texture] Failed to create sampler: {}", e.what());
    return false;
  }
}

uint32_t Texture::getLayerCount() const {
  std::lock_guard<std::mutex> lock(textureMutex_);
  return static_cast<uint32_t>(layers_.size());
}

vk::Sampler Texture::getSampler() const {
  std::lock_guard<std::mutex> lock(textureMutex_);
  return sampler_ ? **sampler_ : vk::Sampler{};
}

const TextureLayer *Texture::getLayer(uint32_t index) const {
  std::lock_guard<std::mutex> lock(textureMutex_);
  if (index >= layers_.size()) {
    return nullptr;
  }
  return &layers_[index];
}

std::shared_ptr<Texture>
Texture::createFallback(device::VMAAllocator &allocator,
                        device::GPUDevice &device) {
  static constexpr TextureTag fallbackTag{"__fallback__", nullptr, 0};
  auto tex = std::make_shared<Texture>(fallbackTag);

  TextureLayer layer;

  constexpr uint32_t width = 1;
  constexpr uint32_t height = 1;
  constexpr uint32_t bytesPerPixel = 4;
  constexpr vk::DeviceSize imageSize = width * height * bytesPerPixel;

  auto stagingBuffer =
      allocator.createStagingBuffer(imageSize, "__fallback_staging");
  if (!stagingBuffer.isValid()) {
    std::println(stderr, "[Texture] Failed to create fallback staging buffer");
    return nullptr;
  }

  void *mapped = stagingBuffer.map();
  if (!mapped) {
    std::println(stderr, "[Texture] Failed to map fallback staging buffer");
    return nullptr;
  }
  const uint8_t whitePixel[4] = {255, 255, 255, 255};
  std::memcpy(mapped, whitePixel, sizeof(whitePixel));
  stagingBuffer.flush();
  stagingBuffer.unmap();

  layer.gpuImage = allocator.createImage2D(
      width, height, vk::Format::eR8G8B8A8Srgb,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
      "__fallback_image");
  if (!layer.gpuImage.isValid()) {
    std::println(stderr, "[Texture] Failed to create fallback GPU image");
    return nullptr;
  }

  if (!allocator.createImageView(layer.gpuImage)) {
    std::println(stderr, "[Texture] Failed to create fallback image view");
    return nullptr;
  }

  if (!device.getQueueFamilies().hasGraphics()) {
    std::println(stderr,
                 "[Texture] No graphics queue for fallback texture upload");
    return nullptr;
  }
  uint32_t graphicsFamily = device.getQueueFamilies().graphicsFamily.value();

  vk::CommandPoolCreateInfo poolInfo{vk::CommandPoolCreateFlagBits::eTransient,
                                     graphicsFamily};
  vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);

  vk::CommandBufferAllocateInfo cmdAllocInfo{
      *cmdPool, vk::CommandBufferLevel::ePrimary, 1};
  vk::raii::CommandBuffers cmdBuffers(device.getRaiiDevice(), cmdAllocInfo);
  auto &cmd = cmdBuffers[0];

  vk::CommandBufferBeginInfo beginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
  cmd.begin(beginInfo);

  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eUndefined;
    barrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = layer.gpuImage.getImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                        barrier);
  }

  vk::BufferImageCopy copyRegion{};
  copyRegion.bufferOffset = 0;
  copyRegion.bufferRowLength = 0;
  copyRegion.bufferImageHeight = 0;
  copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  copyRegion.imageSubresource.mipLevel = 0;
  copyRegion.imageSubresource.baseArrayLayer = 0;
  copyRegion.imageSubresource.layerCount = 1;
  copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
  copyRegion.imageExtent = vk::Extent3D{width, height, 1};

  cmd.copyBufferToImage(stagingBuffer.getBuffer(), layer.gpuImage.getImage(),
                        vk::ImageLayout::eTransferDstOptimal, copyRegion);

  {
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = layer.gpuImage.getImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {},
                        barrier);
  }

  cmd.end();

  vk::SubmitInfo submitInfo{};
  submitInfo.commandBufferCount = 1;
  vk::CommandBuffer rawCmd = *cmd;
  submitInfo.pCommandBuffers = &rawCmd;

  device.getGraphicsQueue().submit(submitInfo);
  device.getGraphicsQueue().waitIdle();

  layer.loaded = true;
  tex->layers_.push_back(std::move(layer));
  tex->uploaded_ = true;
  tex->createSampler(device);

  std::println("[Texture] Created fallback texture (1x1 white)");
  return tex;
}

} // namespace window
