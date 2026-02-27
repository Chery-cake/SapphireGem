#include "image_array_registry.h"
#include "vulkan_device.h"
#include <print>

namespace device {

// ============================================================================
// Initialisation / shutdown
// ============================================================================

bool ImageArrayRegistry::initialize(GPUDevice &device) {
  if (initialized_) {
    return true;
  }

  // --- Probe for descriptor indexing support (Vulkan 1.2+) ---
  auto physProps =
      device.getRaiiPhysicalDevice()
          .getProperties2<vk::PhysicalDeviceProperties2,
                          vk::PhysicalDeviceVulkan12Properties>();
  (void)physProps; // properties not needed for the check itself

  auto physFeatures =
      device.getRaiiPhysicalDevice()
          .getFeatures2<vk::PhysicalDeviceFeatures2,
                        vk::PhysicalDeviceVulkan12Features>();
  const auto &vk12 = physFeatures.get<vk::PhysicalDeviceVulkan12Features>();

  bindlessSupported_ =
      (vk12.descriptorIndexing == VK_TRUE) &&
      (vk12.shaderSampledImageArrayNonUniformIndexing == VK_TRUE) &&
      (vk12.runtimeDescriptorArray == VK_TRUE) &&
      (vk12.descriptorBindingPartiallyBound == VK_TRUE) &&
      (vk12.descriptorBindingVariableDescriptorCount == VK_TRUE);

  if (!bindlessSupported_) {
    std::println("[ImageArrayRegistry] Descriptor indexing NOT supported – "
                 "using fixed-size fallback (max 16 images per kind)");
  } else {
    std::println("[ImageArrayRegistry] Descriptor indexing supported – "
                 "bindless mode enabled (up to {} images per kind)",
                 kMaxImages);
  }

  // --- Create descriptor set layout ---
  descriptorSetLayout_ =
      createBindlessSetLayout(device, bindlessSupported_);
  if (!descriptorSetLayout_) {
    std::println(stderr,
                 "[ImageArrayRegistry] Failed to create descriptor set layout");
    return false;
  }

  // --- Descriptor pool ---
  const uint32_t maxImagesPerKind = bindlessSupported_ ? kMaxImages : 16;
  std::vector<vk::DescriptorPoolSize> poolSizes = {
      // 3 image arrays
      {vk::DescriptorType::eSampledImage, kKindCount * maxImagesPerKind},
      // 1 sampler
      {vk::DescriptorType::eSampler, 1},
      // 2 SSBOs (records + layers)
      {vk::DescriptorType::eStorageBuffer, 2},
  };

  vk::DescriptorPoolCreateInfo poolInfo{
      vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind, 1,
      static_cast<uint32_t>(poolSizes.size()), poolSizes.data()};

  try {
    descriptorPool_ = std::make_unique<vk::raii::DescriptorPool>(
        device.getRaiiDevice(), poolInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[ImageArrayRegistry] Failed to create descriptor pool: {}",
                 e.what());
    return false;
  }

  // --- Allocate descriptor set ---
  uint32_t variableCounts[1] = {maxImagesPerKind};
  vk::DescriptorSetVariableDescriptorCountAllocateInfo variableInfo{1,
                                                                    variableCounts};

  vk::DescriptorSetLayout layouts[] = {**descriptorSetLayout_};
  vk::DescriptorSetAllocateInfo allocInfo{**descriptorPool_, 1, layouts};
  if (bindlessSupported_) {
    allocInfo.pNext = &variableInfo;
  }

  try {
    auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(), allocInfo);
    descriptorSets_.reserve(sets.size());
    for (auto &s : sets) {
      descriptorSets_.push_back(std::move(s));
    }
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[ImageArrayRegistry] Failed to allocate descriptor set: {}",
                 e.what());
    return false;
  }

  initialized_ = true;
  std::println("[ImageArrayRegistry] Initialised for device '{}'",
               device.getInfo().name);
  return true;
}

void ImageArrayRegistry::shutdown() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  descriptorSetLayout_.reset();
  for (auto &arr : imageArrays_) {
    arr.clear();
  }
  initialized_ = false;
}

// ============================================================================
// Image registration
// ============================================================================

ImageHandle ImageArrayRegistry::registerImage(ImageKind kind,
                                              vk::ImageView view) {
  std::lock_guard<std::mutex> lock(registryMutex_);

  uint32_t kindIdx = static_cast<uint32_t>(kind);
  if (kindIdx >= kKindCount) {
    std::println(stderr,
                 "[ImageArrayRegistry] Invalid ImageKind {}", kindIdx);
    return {};
  }

  auto &arr = imageArrays_[kindIdx];
  ImageHandle handle;
  handle.index = static_cast<uint32_t>(arr.size());
  arr.push_back({view, false});
  return handle;
}

// ============================================================================
// Descriptor commit
// ============================================================================

void ImageArrayRegistry::commitDescriptors(
    GPUDevice &device, vk::Sampler sampler,
    const AllocatedBuffer *recordBuffer,
    const AllocatedBuffer *layerBuffer) {
  std::lock_guard<std::mutex> lock(registryMutex_);

  if (!initialized_ || descriptorSets_.empty()) {
    return;
  }

  std::vector<vk::WriteDescriptorSet> writes;
  // We need to keep imageInfos alive until updateDescriptorSets
  std::vector<std::vector<vk::DescriptorImageInfo>> allImageInfos(kKindCount);

  // --- Image array bindings (bindings 0, 1, 2) ---
  for (uint32_t k = 0; k < kKindCount; ++k) {
    auto &arr = imageArrays_[k];
    auto &infos = allImageInfos[k];
    infos.reserve(arr.size());

    for (size_t i = 0; i < arr.size(); ++i) {
      if (!arr[i].committed) {
        vk::DescriptorImageInfo imgInfo;
        imgInfo.imageView = arr[i].view;
        imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        infos.push_back(imgInfo);
        arr[i].committed = true;
      }
    }

    if (infos.empty()) {
      continue;
    }

    // Write all images for this kind at once
    // For incremental updates, we'd track the starting dstArrayElement;
    // here we write from element 0 for simplicity (full overwrite).
    // Re-collect all infos for a full write:
    infos.clear();
    for (auto &entry : arr) {
      vk::DescriptorImageInfo imgInfo;
      imgInfo.imageView = entry.view;
      imgInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
      infos.push_back(imgInfo);
    }

    vk::WriteDescriptorSet w{};
    w.dstSet = *descriptorSets_[0];
    w.dstBinding = k; // binding 0/1/2 for each kind
    w.dstArrayElement = 0;
    w.descriptorCount = static_cast<uint32_t>(infos.size());
    w.descriptorType = vk::DescriptorType::eSampledImage;
    w.pImageInfo = infos.data();
    writes.push_back(w);
  }

  // --- Sampler binding (binding 3) ---
  vk::DescriptorImageInfo samplerInfo;
  samplerInfo.sampler = sampler;
  {
    vk::WriteDescriptorSet w{};
    w.dstSet = *descriptorSets_[0];
    w.dstBinding = kBindingSampler;
    w.dstArrayElement = 0;
    w.descriptorCount = 1;
    w.descriptorType = vk::DescriptorType::eSampler;
    w.pImageInfo = &samplerInfo;
    writes.push_back(w);
  }

  // --- SSBO bindings (binding 4, 5) ---
  vk::DescriptorBufferInfo recBufInfo{};
  if (recordBuffer && recordBuffer->isValid()) {
    recBufInfo.buffer = recordBuffer->getBuffer();
    recBufInfo.offset = 0;
    recBufInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet w{};
    w.dstSet = *descriptorSets_[0];
    w.dstBinding = kBindingTextureRecords;
    w.dstArrayElement = 0;
    w.descriptorCount = 1;
    w.descriptorType = vk::DescriptorType::eStorageBuffer;
    w.pBufferInfo = &recBufInfo;
    writes.push_back(w);
  }

  vk::DescriptorBufferInfo layBufInfo{};
  if (layerBuffer && layerBuffer->isValid()) {
    layBufInfo.buffer = layerBuffer->getBuffer();
    layBufInfo.offset = 0;
    layBufInfo.range = VK_WHOLE_SIZE;

    vk::WriteDescriptorSet w{};
    w.dstSet = *descriptorSets_[0];
    w.dstBinding = kBindingTextureLayers;
    w.dstArrayElement = 0;
    w.descriptorCount = 1;
    w.descriptorType = vk::DescriptorType::eStorageBuffer;
    w.pBufferInfo = &layBufInfo;
    writes.push_back(w);
  }

  if (!writes.empty()) {
    device.getRaiiDevice().updateDescriptorSets(writes, {});
  }

  std::println("[ImageArrayRegistry] Committed descriptors – "
               "images2D={}, atlases={}, maps={}",
               imageArrays_[0].size(), imageArrays_[1].size(),
               imageArrays_[2].size());
}

// ============================================================================
// Accessors
// ============================================================================

vk::DescriptorSet ImageArrayRegistry::getDescriptorSet() const {
  if (descriptorSets_.empty()) {
    return {};
  }
  return *descriptorSets_[0];
}

vk::DescriptorSetLayout ImageArrayRegistry::getDescriptorSetLayout() const {
  return descriptorSetLayout_ ? **descriptorSetLayout_
                              : vk::DescriptorSetLayout{};
}

uint32_t ImageArrayRegistry::getImageCount(ImageKind kind) const {
  std::lock_guard<std::mutex> lock(registryMutex_);
  uint32_t k = static_cast<uint32_t>(kind);
  if (k >= kKindCount) {
    return 0;
  }
  return static_cast<uint32_t>(imageArrays_[k].size());
}

// ============================================================================
// Static helper – descriptor set layout creation
// ============================================================================

std::unique_ptr<vk::raii::DescriptorSetLayout>
ImageArrayRegistry::createBindlessSetLayout(GPUDevice &device,
                                            bool bindlessSupported) {
  const uint32_t maxCount = bindlessSupported ? kMaxImages : 16;

  // Flags per binding
  std::vector<vk::DescriptorBindingFlags> bindingFlags;

  vk::DescriptorBindingFlags imageFlags =
      bindlessSupported
          ? (vk::DescriptorBindingFlagBits::ePartiallyBound |
             vk::DescriptorBindingFlagBits::eUpdateAfterBind)
          : vk::DescriptorBindingFlags{};

  vk::DescriptorBindingFlags lastImageFlags =
      bindlessSupported
          ? (vk::DescriptorBindingFlagBits::ePartiallyBound |
             vk::DescriptorBindingFlagBits::eUpdateAfterBind |
             vk::DescriptorBindingFlagBits::eVariableDescriptorCount)
          : vk::DescriptorBindingFlags{};

  vk::DescriptorBindingFlags noFlags{};

  std::vector<vk::DescriptorSetLayoutBinding> bindings;

  // Binding 0: images2D[]
  bindings.push_back({kBindingImages2D, vk::DescriptorType::eSampledImage,
                      maxCount,
                      vk::ShaderStageFlagBits::eFragment |
                          vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(imageFlags);

  // Binding 1: atlases[]
  bindings.push_back({kBindingAtlases, vk::DescriptorType::eSampledImage,
                      maxCount,
                      vk::ShaderStageFlagBits::eFragment |
                          vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(imageFlags);

  // Binding 2: maps[] – last image array, uses variable count
  bindings.push_back({kBindingMaps, vk::DescriptorType::eSampledImage,
                      maxCount,
                      vk::ShaderStageFlagBits::eFragment |
                          vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(lastImageFlags);

  // Binding 3: shared sampler
  bindings.push_back({kBindingSampler, vk::DescriptorType::eSampler, 1,
                      vk::ShaderStageFlagBits::eFragment |
                          vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(noFlags);

  // Binding 4: TextureRecord SSBO
  bindings.push_back(
      {kBindingTextureRecords, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex |
           vk::ShaderStageFlagBits::eGeometry |
           vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(noFlags);

  // Binding 5: TextureLayer SSBO
  bindings.push_back(
      {kBindingTextureLayers, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex |
           vk::ShaderStageFlagBits::eGeometry |
           vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute});
  bindingFlags.push_back(noFlags);

  vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
      static_cast<uint32_t>(bindingFlags.size()), bindingFlags.data()};

  vk::DescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  if (bindlessSupported) {
    layoutInfo.flags =
        vk::DescriptorSetLayoutCreateFlagBits::eUpdateAfterBindPool;
    layoutInfo.pNext = &flagsInfo;
  }

  try {
    return std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[ImageArrayRegistry] createBindlessSetLayout failed: {}",
                 e.what());
    return nullptr;
  }
}

} // namespace device
