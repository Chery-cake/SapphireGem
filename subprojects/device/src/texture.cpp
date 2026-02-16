#include "texture.h"
#include <print>

namespace device {

Texture::Texture(const TextureTag &tag)
    : name_(tag.name) {
  layers_.reserve(tag.layerCount);
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

bool Texture::upload(VMAAllocator &allocator, GPUDevice &device) {
  std::lock_guard<std::mutex> lock(textureMutex_);

  if (uploaded_) {
    return true;
  }

  for (auto &layer : layers_) {
    if (!layer.imageTag) {
      std::println(stderr, "[Texture] Layer has no image tag in texture: {}",
                   name_);
      continue;
    }

    // Determine dimensions
    uint32_t width = layer.imageTag->width > 0 ? layer.imageTag->width : 256;
    uint32_t height = layer.imageTag->height > 0 ? layer.imageTag->height : 256;

    if (layer.imageTag->isAtlasRegion) {
      width = layer.imageTag->atlasW > 0 ? layer.imageTag->atlasW : width;
      height = layer.imageTag->atlasH > 0 ? layer.imageTag->atlasH : height;
    }

    // Create GPU image
    layer.gpuImage = allocator.createImage2D(
        width, height, vk::Format::eR8G8B8A8Srgb,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        std::string(layer.imageTag->name) + "_image");

    if (!layer.gpuImage.isValid()) {
      std::println(stderr, "[Texture] Failed to create GPU image for layer: {}",
                   layer.imageTag->name);
      return false;
    }

    // Create image view
    if (!allocator.createImageView(layer.gpuImage)) {
      std::println(stderr,
                   "[Texture] Failed to create image view for layer: {}",
                   layer.imageTag->name);
      return false;
    }

    layer.loaded = true;
    std::println("[Texture] Uploaded layer: {} ({}x{})", layer.imageTag->name,
                 width, height);

    // Suppress unused parameter warning
    (void)device;
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

bool Texture::createSampler(GPUDevice &device) {
  std::lock_guard<std::mutex> lock(textureMutex_);

  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
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

} // namespace device
