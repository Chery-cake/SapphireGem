#include "texture_table.h"
#include "vulkan_device.h"
#include <cstring>
#include <print>

namespace device {

TextureId TextureTableManager::addRecord(uint32_t layerCount) {
  std::lock_guard<std::mutex> lock(tableMutex_);

  TextureId id;
  id.index = static_cast<uint32_t>(records_.size());

  GPUTextureRecord rec{};
  rec.firstLayer = static_cast<uint32_t>(layers_.size());
  rec.layerCount = layerCount;
  rec.flags = 0;
  rec._pad0 = 0;
  records_.push_back(rec);

  // Reserve space for the layers (caller fills via setLayers)
  layers_.resize(layers_.size() + layerCount);

  uploaded_ = false;
  return id;
}

void TextureTableManager::setLayers(TextureId id,
                                    const std::vector<GPUTextureLayer> &layers) {
  std::lock_guard<std::mutex> lock(tableMutex_);

  if (!id.isValid() || id.index >= records_.size()) {
    std::println(stderr,
                 "[TextureTable] setLayers: invalid TextureId {}", id.index);
    return;
  }

  const auto &rec = records_[id.index];
  if (layers.size() != rec.layerCount) {
    std::println(stderr,
                 "[TextureTable] setLayers: expected {} layers, got {}",
                 rec.layerCount, layers.size());
    return;
  }

  std::memcpy(&layers_[rec.firstLayer], layers.data(),
              layers.size() * sizeof(GPUTextureLayer));
  uploaded_ = false;
}

bool TextureTableManager::uploadToGPU(VMAAllocator &allocator,
                                      GPUDevice &device) {
  std::lock_guard<std::mutex> lock(tableMutex_);

  if (records_.empty()) {
    uploaded_ = true;
    return true;
  }

  // --- Record SSBO ---
  {
    vk::DeviceSize size = records_.size() * sizeof(GPUTextureRecord);
    auto staging = allocator.createStagingBuffer(size, "tex_record_staging");
    if (!staging.isValid()) {
      std::println(stderr, "[TextureTable] Failed to create record staging");
      return false;
    }

    void *mapped = staging.map();
    if (mapped) {
      std::memcpy(mapped, records_.data(), static_cast<size_t>(size));
      staging.unmap();
    }

    recordBuffer_ = allocator.createStorageBuffer(size, "tex_record_ssbo");
    if (!recordBuffer_.isValid()) {
      std::println(stderr, "[TextureTable] Failed to create record SSBO");
      return false;
    }

    // Copy staging → device-local via one-shot command buffer
    auto qf = device.getQueueFamilies();
    vk::CommandPoolCreateInfo poolInfo{
        vk::CommandPoolCreateFlagBits::eTransient,
        qf.graphicsFamily.value_or(0)};
    vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);
    vk::CommandBufferAllocateInfo cmdAlloc(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
    auto cmdBufs = vk::raii::CommandBuffers(device.getRaiiDevice(), cmdAlloc);
    auto &cmd = cmdBufs[0];

    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::BufferCopy region{0, 0, size};
    cmd.copyBuffer(staging.getBuffer(), recordBuffer_.getBuffer(), region);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.setCommandBuffers(*cmd);
    device.getGraphicsQueue().submit(submit);
    device.getGraphicsQueue().waitIdle();
  }

  // --- Layer SSBO ---
  if (!layers_.empty()) {
    vk::DeviceSize size = layers_.size() * sizeof(GPUTextureLayer);
    auto staging = allocator.createStagingBuffer(size, "tex_layer_staging");
    if (!staging.isValid()) {
      std::println(stderr, "[TextureTable] Failed to create layer staging");
      return false;
    }

    void *mapped = staging.map();
    if (mapped) {
      std::memcpy(mapped, layers_.data(), static_cast<size_t>(size));
      staging.unmap();
    }

    layerBuffer_ = allocator.createStorageBuffer(size, "tex_layer_ssbo");
    if (!layerBuffer_.isValid()) {
      std::println(stderr, "[TextureTable] Failed to create layer SSBO");
      return false;
    }

    auto qf = device.getQueueFamilies();
    vk::CommandPoolCreateInfo poolInfo{
        vk::CommandPoolCreateFlagBits::eTransient,
        qf.graphicsFamily.value_or(0)};
    vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);
    vk::CommandBufferAllocateInfo cmdAlloc(*cmdPool, vk::CommandBufferLevel::ePrimary, 1);
    auto cmdBufs = vk::raii::CommandBuffers(device.getRaiiDevice(), cmdAlloc);
    auto &cmd = cmdBufs[0];

    cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    vk::BufferCopy region{0, 0, size};
    cmd.copyBuffer(staging.getBuffer(), layerBuffer_.getBuffer(), region);
    cmd.end();

    vk::SubmitInfo submit{};
    submit.setCommandBuffers(*cmd);
    device.getGraphicsQueue().submit(submit);
    device.getGraphicsQueue().waitIdle();
  }

  uploaded_ = true;
  std::println("[TextureTable] Uploaded {} records, {} layers",
               records_.size(), layers_.size());
  return true;
}

uint32_t TextureTableManager::getRecordCount() const {
  std::lock_guard<std::mutex> lock(tableMutex_);
  return static_cast<uint32_t>(records_.size());
}

uint32_t TextureTableManager::getLayerCount() const {
  std::lock_guard<std::mutex> lock(tableMutex_);
  return static_cast<uint32_t>(layers_.size());
}

void TextureTableManager::clear() {
  std::lock_guard<std::mutex> lock(tableMutex_);
  records_.clear();
  layers_.clear();
  recordBuffer_ = {};
  layerBuffer_ = {};
  uploaded_ = false;
}

} // namespace device
