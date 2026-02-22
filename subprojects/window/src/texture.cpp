#include "texture.h"
#include "vulkan_device.h"
#include <mutex>
#include <print>

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

    // Transition image layout from UNDEFINED to SHADER_READ_ONLY_OPTIMAL
    // so the image can be sampled in fragment shaders.
    {
      if (!device.getQueueFamilies().hasGraphics()) {
        std::println(stderr,
                     "[Texture] No graphics queue family available for "
                     "layout transition: {}",
                     layer.imageTag->name);
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

      vk::ImageMemoryBarrier barrier{};
      barrier.oldLayout = vk::ImageLayout::eUndefined;
      barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = layer.gpuImage.getImage();
      barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
      barrier.subresourceRange.baseMipLevel = 0;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.baseArrayLayer = 0;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = {};
      barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;

      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                          vk::PipelineStageFlagBits::eFragmentShader, {}, {},
                          {}, barrier);

      cmd.end();

      vk::SubmitInfo submitInfo{};
      submitInfo.commandBufferCount = 1;
      vk::CommandBuffer rawCmd = *cmd;
      submitInfo.pCommandBuffers = &rawCmd;

      device.getGraphicsQueue().submit(submitInfo);
      device.getGraphicsQueue().waitIdle();
    }

    layer.loaded = true;
    std::println("[Texture] Uploaded layer: {} ({}x{})", layer.imageTag->name,
                 width, height);
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

} // namespace window
