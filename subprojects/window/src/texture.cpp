#include "texture.h"
#include "SDL3_image/SDL_image.h"
#include "vulkan_device.h"
#include <mutex>
#include <print>
#include <unordered_map>

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

  // Cache loaded atlas surfaces so the same atlas file is loaded only once
  std::unordered_map<const char *, SDL_Surface *> atlasCache;
  auto cleanupCache = [&atlasCache]() {
    for (auto &[path, surf] : atlasCache) {
      SDL_DestroySurface(surf);
    }
    atlasCache.clear();
  };

  for (auto &layer : layers_) {
    if (!layer.imageTag) {
      std::println(stderr, "[Texture] Layer has no image tag in texture: {}",
                   name_);
      continue;
    }

    // Resolve the file path: atlas regions use atlasSource->path
    const char *filePath = layer.imageTag->path;
    if (layer.imageTag->isAtlasRegion && layer.imageTag->atlasSource) {
      filePath = layer.imageTag->atlasSource->path;
    }

    if (!filePath) {
      std::println(stderr,
                   "[Texture] No file path for image '{}' in texture: {}",
                   layer.imageTag->name, name_);
      cleanupCache();
      return false;
    }

    // Load or reuse cached surface for atlas sources
    SDL_Surface *rgbaSurface = nullptr;
    bool ownsSurface = false;

    if (layer.imageTag->isAtlasRegion && layer.imageTag->atlasSource) {
      // Atlas region: load atlas once, reuse for all regions
      auto it = atlasCache.find(filePath);
      if (it != atlasCache.end()) {
        rgbaSurface = it->second;
      } else {
        SDL_Surface *loadedSurface = IMG_Load(filePath);
        if (!loadedSurface) {
          std::println(stderr, "[Texture] Failed to load atlas '{}': {}",
                       filePath, SDL_GetError());
          cleanupCache();
          return false;
        }

        if (loadedSurface->format != SDL_PIXELFORMAT_RGBA32) {
          rgbaSurface =
              SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
          SDL_DestroySurface(loadedSurface);
          if (!rgbaSurface) {
            std::println(
                stderr,
                "[Texture] Failed to convert atlas '{}' to RGBA: {}",
                filePath, SDL_GetError());
            cleanupCache();
            return false;
          }
        } else {
          rgbaSurface = loadedSurface;
        }

        atlasCache[filePath] = rgbaSurface;
      }
      // Atlas surfaces are freed by cleanupCache, not per-layer
    } else {
      // Standalone image: load fresh
      SDL_Surface *loadedSurface = IMG_Load(filePath);
      if (!loadedSurface) {
        std::println(stderr, "[Texture] Failed to load image '{}': {}",
                     filePath, SDL_GetError());
        cleanupCache();
        return false;
      }

      if (loadedSurface->format != SDL_PIXELFORMAT_RGBA32) {
        rgbaSurface =
            SDL_ConvertSurface(loadedSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(loadedSurface);
        if (!rgbaSurface) {
          std::println(stderr,
                       "[Texture] Failed to convert image '{}' to RGBA: {}",
                       filePath, SDL_GetError());
          cleanupCache();
          return false;
        }
      } else {
        rgbaSurface = loadedSurface;
      }
      ownsSurface = true;
    }

    // Determine source region and final dimensions
    uint32_t srcX = 0, srcY = 0;
    uint32_t width, height;
    uint32_t surfW = static_cast<uint32_t>(rgbaSurface->w);
    uint32_t surfH = static_cast<uint32_t>(rgbaSurface->h);

    if (layer.imageTag->isAtlasRegion) {
      srcX = layer.imageTag->atlasX;
      srcY = layer.imageTag->atlasY;
      width = layer.imageTag->atlasW > 0 ? layer.imageTag->atlasW : surfW;
      height = layer.imageTag->atlasH > 0 ? layer.imageTag->atlasH : surfH;

      // Validate atlas region fits within the loaded surface
      if (srcX + width > surfW || srcY + height > surfH) {
        std::println(stderr,
                     "[Texture] Atlas region ({}+{}, {}+{}) exceeds surface "
                     "dimensions ({}x{}) for: {}",
                     srcX, width, srcY, height, surfW, surfH,
                     layer.imageTag->name);
        if (ownsSurface) {
          SDL_DestroySurface(rgbaSurface);
        }
        cleanupCache();
        return false;
      }
    } else {
      width = layer.imageTag->width > 0 ? layer.imageTag->width : surfW;
      height = layer.imageTag->height > 0 ? layer.imageTag->height : surfH;

      // Clamp to actual surface dimensions
      if (width > surfW) {
        width = surfW;
      }
      if (height > surfH) {
        height = surfH;
      }
    }

    constexpr uint32_t bytesPerPixel = 4; // RGBA8
    vk::DeviceSize imageSize =
        static_cast<vk::DeviceSize>(width) * height * bytesPerPixel;

    // Create Vulkan staging buffer for CPU-to-GPU transfer
    auto stagingBuffer = allocator.createStagingBuffer(
        imageSize, std::string(layer.imageTag->name) + "_staging");
    if (!stagingBuffer.isValid()) {
      std::println(stderr, "[Texture] Failed to create staging buffer for: {}",
                   layer.imageTag->name);
      if (ownsSurface) {
        SDL_DestroySurface(rgbaSurface);
      }
      cleanupCache();
      return false;
    }

    // Copy pixel data to staging buffer, respecting surface pitch/stride
    {
      void *mapped = stagingBuffer.map();
      if (!mapped) {
        std::println(stderr, "[Texture] Failed to map staging buffer for: {}",
                     layer.imageTag->name);
        if (ownsSurface) {
          SDL_DestroySurface(rgbaSurface);
        }
        cleanupCache();
        return false;
      }

      const auto *srcPixels = static_cast<const uint8_t *>(rgbaSurface->pixels);
      auto *dstPixels = static_cast<uint8_t *>(mapped);
      size_t srcPitch = static_cast<size_t>(rgbaSurface->pitch);
      uint32_t dstRowSize = width * bytesPerPixel;

      for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *srcRow = srcPixels +
                                static_cast<size_t>(srcY + row) * srcPitch +
                                static_cast<size_t>(srcX) * bytesPerPixel;
        uint8_t *dstRow = dstPixels + static_cast<size_t>(row) * dstRowSize;
        std::memcpy(dstRow, srcRow, dstRowSize);
      }

      stagingBuffer.flush();
      stagingBuffer.unmap();
    }

    // Free standalone image surfaces (atlas surfaces freed by cleanupCache)
    if (ownsSurface) {
      SDL_DestroySurface(rgbaSurface);
    }

    // Create GPU image
    layer.gpuImage = allocator.createImage2D(
        width, height, vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        std::string(layer.imageTag->name) + "_image");

    if (!layer.gpuImage.isValid()) {
      std::println(stderr, "[Texture] Failed to create GPU image for layer: {}",
                   layer.imageTag->name);
      cleanupCache();
      return false;
    }

    // Create image view
    if (!allocator.createImageView(layer.gpuImage)) {
      std::println(stderr,
                   "[Texture] Failed to create image view for layer: {}",
                   layer.imageTag->name);
      cleanupCache();
      return false;
    }

    // Upload staging buffer to GPU image using one-time command buffer
    {
      if (!device.getQueueFamilies().hasGraphics()) {
        std::println(stderr,
                     "[Texture] No graphics queue family available for "
                     "upload: {}",
                     layer.imageTag->name);
        cleanupCache();
        return false;
      }
      uint32_t graphicsFamily =
          device.getQueueFamilies().graphicsFamily.value();

      vk::CommandPoolCreateInfo poolInfo{
          vk::CommandPoolCreateFlagBits::eTransient, graphicsFamily};
      vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);

      vk::CommandBufferAllocateInfo cmdAllocInfo{
          *cmdPool, vk::CommandBufferLevel::ePrimary, 1};
      vk::raii::CommandBuffers cmdBuffers(device.getRaiiDevice(), cmdAllocInfo);
      auto &cmd = cmdBuffers[0];

      vk::CommandBufferBeginInfo beginInfo{
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
      cmd.begin(beginInfo);

      // Transition: UNDEFINED -> TRANSFER_DST_OPTIMAL
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

      // Copy staging buffer to GPU image
      vk::BufferImageCopy copyRegion{};
      copyRegion.bufferOffset = 0;
      copyRegion.bufferRowLength = 0;   // Tightly packed
      copyRegion.bufferImageHeight = 0; // Tightly packed
      copyRegion.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
      copyRegion.imageSubresource.mipLevel = 0;
      copyRegion.imageSubresource.baseArrayLayer = 0;
      copyRegion.imageSubresource.layerCount = 1;
      copyRegion.imageOffset = vk::Offset3D{0, 0, 0};
      copyRegion.imageExtent = vk::Extent3D{width, height, 1};

      cmd.copyBufferToImage(stagingBuffer.getBuffer(),
                            layer.gpuImage.getImage(),
                            vk::ImageLayout::eTransferDstOptimal, copyRegion);

      // Transition: TRANSFER_DST_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL
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
                            vk::PipelineStageFlagBits::eFragmentShader, {}, {},
                            {}, barrier);
      }

      cmd.end();

      vk::SubmitInfo submitInfo{};
      submitInfo.commandBufferCount = 1;
      vk::CommandBuffer rawCmd = *cmd;
      submitInfo.pCommandBuffers = &rawCmd;

      device.getGraphicsQueue().submit(submitInfo);
      device.getGraphicsQueue().waitIdle();
    }
    // Staging buffer is automatically freed when it goes out of scope
    // (RAII)

    layer.loaded = true;
    std::println("[Texture] Uploaded layer: {} ({}x{})", layer.imageTag->name,
                 width, height);
  }

  // Free all cached atlas surfaces
  cleanupCache();

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

} // namespace window
