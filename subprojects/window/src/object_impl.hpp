#pragma once
#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include <print>

namespace window {

// ============================================================================
// Construction / Destruction
// ============================================================================

template <uint32_t Dim>
Object<Dim>::Object(const ObjectTag &tag, std::vector<VertexType> vertices,
                    std::vector<uint32_t> indices)
    : name_(tag.name), baseMaterialTag_(tag.baseMaterialTag),
      vertices_(std::move(vertices)), indices_(std::move(indices)) {
  // Initialize transforms: position and rotation to 0, scale to 1
  position_.fill(0.0f);
  rotation_.fill(0.0f);
  scale_.fill(1.0f);

  // Auto-calculate faces from indices
  calculateFaces();
}

template <uint32_t Dim> Object<Dim>::~Object() { release(); }

// ============================================================================
// Move semantics
// ============================================================================

template <uint32_t Dim> Object<Dim>::Object(Object &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.objectMutex_);
  name_ = other.name_;
  baseMaterialTag_ = other.baseMaterialTag_;
  position_ = other.position_;
  rotation_ = other.rotation_;
  scale_ = other.scale_;
  vertices_ = std::move(other.vertices_);
  indices_ = std::move(other.indices_);
  faces_ = std::move(other.faces_);
  faceTextures_ = std::move(other.faceTextures_);
  textureSlots_ = std::move(other.textureSlots_);
  fallbackTexture_ = std::move(other.fallbackTexture_);
  objectPipeline_ = std::move(other.objectPipeline_);
  pipelineConfig_ = other.pipelineConfig_;
  time_ = other.time_;
  uniformBuffers_ = std::move(other.uniformBuffers_);
  descriptorPool_ = std::move(other.descriptorPool_);
  descriptorSets_ = std::move(other.descriptorSets_);
  descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
  initialized_ = other.initialized_;
  other.initialized_ = false;
}

template <uint32_t Dim>
Object<Dim> &Object<Dim>::operator=(Object &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(objectMutex_, other.objectMutex_);
    release();
    name_ = other.name_;
    baseMaterialTag_ = other.baseMaterialTag_;
    position_ = other.position_;
    rotation_ = other.rotation_;
    scale_ = other.scale_;
    vertices_ = std::move(other.vertices_);
    indices_ = std::move(other.indices_);
    faces_ = std::move(other.faces_);
    faceTextures_ = std::move(other.faceTextures_);
    textureSlots_ = std::move(other.textureSlots_);
    fallbackTexture_ = std::move(other.fallbackTexture_);
    objectPipeline_ = std::move(other.objectPipeline_);
    pipelineConfig_ = other.pipelineConfig_;
    time_ = other.time_;
    uniformBuffers_ = std::move(other.uniformBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

// ============================================================================
// Per-face texture management
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::setFaceTexture(uint32_t faceIndex,
                                 std::shared_ptr<Texture> texture) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (texture) {
    faceTextures_[faceIndex] = std::move(texture);
  } else {
    faceTextures_.erase(faceIndex);
  }
}

template <uint32_t Dim>
std::shared_ptr<Texture> Object<Dim>::getFaceTexture(uint32_t faceIndex) const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  auto it = faceTextures_.find(faceIndex);
  if (it != faceTextures_.end()) {
    return it->second;
  }
  return nullptr;
}

template <uint32_t Dim>
std::vector<std::shared_ptr<Texture>> Object<Dim>::getUniqueTextures() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  std::vector<std::shared_ptr<Texture>> unique;
  for (const auto &[faceIdx, tex] : faceTextures_) {
    if (tex) {
      bool found = false;
      for (const auto &existing : unique) {
        if (existing.get() == tex.get()) {
          found = true;
          break;
        }
      }
      if (!found) {
        unique.push_back(tex);
      }
    }
  }
  return unique;
}

template <uint32_t Dim> uint32_t Object<Dim>::getTextureSlotCount() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return static_cast<uint32_t>(textureSlots_.size());
}

template <uint32_t Dim>
void Object<Dim>::setTextureBindings(
    std::vector<std::shared_ptr<Texture>> bindings) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  textureSlots_ = std::move(bindings);
}

// ============================================================================
// Face calculation
// ============================================================================

template <uint32_t Dim> void Object<Dim>::calculateFaces() {
  faces_.clear();

  if (indices_.empty()) {
    // No indices: treat each set of 3 vertices as one face
    uint32_t totalVerts = static_cast<uint32_t>(vertices_.size());
    uint32_t faceCount = totalVerts / 3;
    faces_.reserve(faceCount);
    for (uint32_t i = 0; i < faceCount; ++i) {
      Face face;
      face.faceIndex = i;
      face.vertexOffset = i * 3;
      face.vertexCount = 3;
      faces_.push_back(face);
    }
  } else {
    // Every 3 consecutive indices form one triangular face
    uint32_t faceCount = static_cast<uint32_t>(indices_.size()) / 3;
    faces_.reserve(faceCount);
    for (uint32_t i = 0; i < faceCount; ++i) {
      Face face;
      face.faceIndex = i;
      face.vertexOffset = i * 3;
      face.vertexCount = 3;
      faces_.push_back(face);
    }
  }
}

// ============================================================================
// Initialization / Release
// ============================================================================

template <uint32_t Dim>
bool Object<Dim>::initialize(device::VMAAllocator &allocator,
                             device::GPUDevice &device, Material &baseMaterial,
                             vk::RenderPass renderPass, uint32_t framesInFlight,
                             const PipelineConfig &pipelineConfig) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (initialized_) {
    std::println(stderr,
                 "[Object] Already initialized: {} - call release() first",
                 name_);
    return false;
  }

  if (!baseMaterialTag_) {
    std::println(stderr, "[Object] No base material tag set for object: {}",
                 name_);
    return false;
  }

  if (!baseMaterial.isInitialized()) {
    std::println(stderr,
                 "[Object] Base material '{}' not initialized for object: {}",
                 baseMaterial.getName(), name_);
    return false;
  }

  if (framesInFlight == 0) {
    std::println(stderr, "[Object] framesInFlight must be > 0 for object: {}",
                 name_);
    return false;
  }

  // Build texture slot list for descriptor binding.
  // If setTextureBindings() was called, textureSlots_ is already populated
  // in the caller-specified order. Otherwise, derive from face textures
  // (legacy path for simple single-texture objects).
  if (textureSlots_.empty()) {
    for (const auto &[faceIdx, tex] : faceTextures_) {
      if (tex) {
        bool found = false;
        for (const auto &existing : textureSlots_) {
          if (existing.get() == tex.get()) {
            found = true;
            break;
          }
        }
        if (!found) {
          textureSlots_.push_back(tex);
        }
      }
    }
  }

  // Replace any null or unuploaded textures with a valid fallback (1×1 white)
  for (auto &tex : textureSlots_) {
    if (!tex || !tex->isUploaded() || !tex->getSampler()) {
      if (!fallbackTexture_) {
        fallbackTexture_ = Texture::createFallback(allocator, device);
        if (!fallbackTexture_) {
          std::println(
              stderr,
              "[Object] Failed to create fallback texture for object: {}",
              name_);
          return false;
        }
      }
      std::println("[Object] Using fallback texture for missing/unuploaded "
                   "slot in '{}'",
                   name_);
      tex = fallbackTexture_;
    }
  }
  uint32_t texCount = static_cast<uint32_t>(textureSlots_.size());

  // Calculate total layer count across all textures for descriptor bindings
  uint32_t totalLayerBindings = 0;
  for (const auto &tex : textureSlots_) {
    totalLayerBindings += tex->getLayerCount();
  }

  // Log binding table for this object's pipeline
  std::println("[Object] Binding table for '{}':", name_);
  std::println("  [0] UBO ({}D, {}B)", Dim, sizeof(GPUUBO));
  {
    uint32_t logIdx = 0;
    for (uint32_t t = 0; t < texCount; ++t) {
      auto &tex = textureSlots_[t];
      uint32_t layerCount = tex->getLayerCount();
      for (uint32_t l = 0; l < layerCount; ++l) {
        const TextureLayer *layer = tex->getLayer(l);
        const char *layerName =
            (layer && layer->imageTag) ? layer->imageTag->getName()
                                       : "(fallback)";
        std::println("  [{}] {} (texture '{}', layer {})", 1 + logIdx,
                     layerName, tex->getName(), l);
        logIdx++;
      }
    }
  }

  // Create descriptor set layout (owned by this object)
  // Binding 0: UBO (uniform buffer)
  // Binding 1..N: Combined image samplers (one per texture layer)
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  bindings.reserve(1 + totalLayerBindings);

  vk::DescriptorSetLayoutBinding uboLayoutBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};
  bindings.push_back(uboLayoutBinding);

  for (uint32_t i = 0; i < totalLayerBindings; ++i) {
    vk::DescriptorSetLayoutBinding texBinding{
        1 + i, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment |
            vk::ShaderStageFlagBits::eGeometry};
    bindings.push_back(texBinding);
  }

  vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{
      {}, static_cast<uint32_t>(bindings.size()), bindings.data()};

  try {
    descriptorSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutCreateInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor set layout for '{}': {}",
                 name_, e.what());
    return false;
  }

  // Create per-object pipeline from base material (pass total layer count)
  objectPipeline_ = baseMaterial.createPipelineForObject(
      device, renderPass, **descriptorSetLayout_, pipelineConfig,
      totalLayerBindings);
  if (!objectPipeline_.isValid()) {
    std::println(stderr, "[Object] Failed to create pipeline for object: {}",
                 name_);
    descriptorSetLayout_.reset();
    return false;
  }

  // Store pipeline config for push constants during draw
  pipelineConfig_ = pipelineConfig;

  // Create uniform buffers (one per frame in flight)
  // UBO size matches the GPU-compatible layout:
  // sizeof(GPUUniformBufferData<Dim>)
  const vk::DeviceSize uboSize = sizeof(GPUUBO);
  uniformBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ubo = allocator.createUniformBuffer(
        uboSize, std::string(name_) + "_ubo_" + std::to_string(i));
    if (!ubo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create uniform buffer {} for '{}'", i,
                   name_);
      release();
      return false;
    }
    uniformBuffers_.push_back(std::move(ubo));
  }

  // Create descriptor pool with enough room for UBO + texture layer samplers
  std::vector<vk::DescriptorPoolSize> poolSizes;
  poolSizes.push_back({vk::DescriptorType::eUniformBuffer, framesInFlight});
  if (totalLayerBindings > 0) {
    poolSizes.push_back({vk::DescriptorType::eCombinedImageSampler,
                         framesInFlight * totalLayerBindings});
  }

  vk::DescriptorPoolCreateInfo poolInfo{
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, framesInFlight,
      static_cast<uint32_t>(poolSizes.size()), poolSizes.data()};

  try {
    descriptorPool_ = std::make_unique<vk::raii::DescriptorPool>(
        device.getRaiiDevice(), poolInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor pool for '{}': {}",
                 name_, e.what());
    release();
    return false;
  }

  // Allocate descriptor sets
  std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                               **descriptorSetLayout_);
  vk::DescriptorSetAllocateInfo allocInfo{**descriptorPool_, framesInFlight,
                                          layouts.data()};

  try {
    auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(), allocInfo);
    descriptorSets_.reserve(sets.size());
    for (auto &s : sets) {
      descriptorSets_.push_back(std::move(s));
    }
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to allocate descriptor sets for '{}': {}",
                 name_, e.what());
    release();
    return false;
  }

  // Update descriptor sets to point to uniform buffers and texture layers
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    std::vector<vk::WriteDescriptorSet> writes;

    // UBO binding (binding 0)
    vk::DescriptorBufferInfo bufferInfo{uniformBuffers_[i].getBuffer(), 0,
                                        uboSize};

    vk::WriteDescriptorSet uboWrite{*descriptorSets_[i],
                                    0,
                                    0,
                                    1,
                                    vk::DescriptorType::eUniformBuffer,
                                    nullptr,
                                    &bufferInfo,
                                    nullptr};
    writes.push_back(uboWrite);

    // Texture layer bindings (binding 1..N, one per layer across all
    // textures). All textures are guaranteed valid (fallback applied above).
    std::vector<vk::DescriptorImageInfo> imageInfos(totalLayerBindings);
    uint32_t bindingIdx = 0;
    for (uint32_t t = 0; t < texCount; ++t) {
      auto &tex = textureSlots_[t];
      uint32_t layerCount = tex->getLayerCount();
      for (uint32_t l = 0; l < layerCount; ++l) {
        const TextureLayer *layer = tex->getLayer(l);
        imageInfos[bindingIdx].sampler = tex->getSampler();
        imageInfos[bindingIdx].imageView =
            (layer && layer->loaded)
                ? layer->gpuImage.getView()
                : (fallbackTexture_ ? fallbackTexture_->getLayer(0)
                                          ->gpuImage.getView()
                                    : vk::ImageView{});
        imageInfos[bindingIdx].imageLayout =
            vk::ImageLayout::eShaderReadOnlyOptimal;

        vk::WriteDescriptorSet texWrite{
            *descriptorSets_[i],
            1 + bindingIdx,
            0,
            1,
            vk::DescriptorType::eCombinedImageSampler,
            &imageInfos[bindingIdx],
            nullptr,
            nullptr};
        writes.push_back(texWrite);
        bindingIdx++;
      }
    }

    device.getRaiiDevice().updateDescriptorSets(writes, {});
  }

  initialized_ = true;
  std::println("[Object] Initialized '{}' ({}D, {} faces, {} textures, "
               "{} layer bindings, {} frames, UBO={}B)",
               name_, Dim, faces_.size(), texCount, totalLayerBindings,
               framesInFlight, uboSize);
  return true;
}

template <uint32_t Dim> void Object<Dim>::release() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  uniformBuffers_.clear();
  objectPipeline_.reset();
  descriptorSetLayout_.reset();
  textureSlots_.clear();
  fallbackTexture_.reset();
  initialized_ = false;
}

// ============================================================================
// Time setter for push constant animation
// ============================================================================

template <uint32_t Dim> void Object<Dim>::setTime(float time) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  time_ = time;
}

// ============================================================================
// Transform accessors (dimension-agnostic)
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::setPosition(const std::array<float, Dim> &pos) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  position_ = pos;
}

template <uint32_t Dim>
void Object<Dim>::setRotation(const std::array<float, Dim> &rot) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  rotation_ = rot;
}

template <uint32_t Dim>
void Object<Dim>::setScale(const std::array<float, Dim> &scl) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  scale_ = scl;
}

template <uint32_t Dim>
std::array<float, Dim> Object<Dim>::getPosition() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return position_;
}

template <uint32_t Dim>
std::array<float, Dim> Object<Dim>::getRotation() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return rotation_;
}

template <uint32_t Dim> std::array<float, Dim> Object<Dim>::getScale() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return scale_;
}

template <uint32_t Dim> const Face *Object<Dim>::getFace(uint32_t index) const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (index >= faces_.size()) {
    return nullptr;
  }
  return &faces_[index];
}

// ============================================================================
// Uniform update
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::updateUniforms(uint32_t frameIndex, const MatType &viewMatrix,
                                 const MatType &projMatrix) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_) {
    return;
  }

  if (frameIndex >= uniformBuffers_.size()) {
    std::println(stderr, "[Object] Invalid frame index {} (max {}) for '{}'",
                 frameIndex, uniformBuffers_.size() - 1, name_);
    return;
  }

  UBO uboData{};
  uboData.model = buildModelMatrix();
  uboData.view = viewMatrix;
  uboData.proj = projMatrix;

  // Convert to GPU-compatible layout (handles std140 padding for mat3)
  GPUUBO gpuData{};
  gpuData.fromUBO(uboData);

  void *mapped = uniformBuffers_[frameIndex].map();
  if (mapped) {
    std::memcpy(mapped, &gpuData, sizeof(GPUUBO));
    uniformBuffers_[frameIndex].unmap();
  }
}

// ============================================================================
// Draw
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::draw(vk::CommandBuffer cmd, uint32_t frameIndex) const {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_) {
    return;
  }

  if (frameIndex >= descriptorSets_.size()) {
    std::println(stderr,
                 "[Object] Invalid frame index {} (max {}) for draw '{}'",
                 frameIndex, descriptorSets_.size() - 1, name_);
    return;
  }

  if (!objectPipeline_.isValid()) {
    std::println(stderr, "[Object] No valid pipeline for draw '{}'", name_);
    return;
  }

  // Bind the single pipeline and descriptor sets
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                   **objectPipeline_.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         **objectPipeline_.pipelineLayout, 0,
                         {*descriptorSets_[frameIndex]}, {});

  // Push time constant if configured
  if (pipelineConfig_.pushConstantSize > 0 &&
      pipelineConfig_.pushConstantSize <= sizeof(time_)) {
    cmd.pushConstants(**objectPipeline_.pipelineLayout,
                      pipelineConfig_.pushConstantStages, 0,
                      pipelineConfig_.pushConstantSize, &time_);
  }

  // Single draw call for all vertices
  uint32_t totalVertices = static_cast<uint32_t>(vertices_.size());
  if (totalVertices > 0) {
    cmd.draw(totalVertices, 1, 0, 0);
  }
}

// ============================================================================
// Model matrix construction (dimension-agnostic)
// ============================================================================

/**
 * @brief Build a (Dim+1)×(Dim+1) model matrix from transforms
 *
 * The algorithm is the same across all dimensions:
 * 1. Start with identity matrix
 * 2. Apply scale (diagonal entries 0..Dim-1)
 * 3. Apply rotation in each axis-pair plane (i,j) for i<j
 * 4. Apply translation (last column, entries 0..Dim-1)
 *
 * For Dim=2: 3×3 matrix, 1 rotation (in XY plane)
 * For Dim=3: 4×4 matrix, 3 rotations (XY, XZ, YZ planes)
 * For Dim=N: (N+1)×(N+1) matrix, N*(N-1)/2 rotations
 */
template <uint32_t Dim>
typename Object<Dim>::MatType Object<Dim>::buildModelMatrix() const {
  constexpr uint32_t N = Dim + 1; // Matrix dimension
  MatType result(1.0f);           // Identity

  // 1. Apply scale: multiply diagonal by scale factors
  for (uint32_t i = 0; i < Dim; ++i) {
    result[i][i] = scale_[i];
  }

  // 2. Apply rotations in each axis-pair plane
  // For Dim dimensions, there are Dim*(Dim-1)/2 rotation planes.
  // The rotation_ array stores one angle per dimension:
  // For Dim=2: rotation_[0] rotates in the XY plane
  // For Dim=3: rotation_[0]=around X (YZ plane), [1]=Y (XZ), [2]=Z (XY)
  //
  // We apply them as Givens rotations, which is dimension-agnostic.
  // For 0 < Dim <= 3, we use the standard axis-rotation convention.
  // For Dim > 3, only the first 3 rotation entries are applied as the
  // standard X/Y/Z rotations; higher-dimensional rotations are not mapped.
  constexpr uint32_t maxRotations = (Dim < 3) ? Dim : 3;
  for (uint32_t i = 0; i < maxRotations; ++i) {
    float angle = rotation_[i];
    if (angle != 0.0f) {
      float c = std::cos(angle);
      float s = std::sin(angle);

      // Determine which axis-pair plane to rotate in
      uint32_t a, b;
      if constexpr (Dim == 1) {
        a = 0;
        b = 0; // No real rotation in 1D
      } else if constexpr (Dim == 2) {
        a = 0;
        b = 1; // XY plane
      } else {
        // Dim >= 3: standard convention
        // rotation_[0] = around X axis (affects Y,Z plane)
        // rotation_[1] = around Y axis (affects X,Z plane)
        // rotation_[2] = around Z axis (affects X,Y plane)
        if (i == 0) {
          a = 1;
          b = 2;
        } else if (i == 1) {
          a = 0;
          b = 2;
        } else {
          a = 0;
          b = 1;
        }
      }

      if constexpr (Dim >= 2) {
        // Apply Givens rotation: rotate columns a and b
        MatType rot(1.0f);
        rot[a][a] = c;
        rot[b][b] = c;
        rot[b][a] = -s;
        rot[a][b] = s;

        // For Y-axis rotation (Dim>=3, i==1), negate to maintain
        // right-hand
        if constexpr (Dim >= 3) {
          if (i == 1) {
            rot[b][a] = s;
            rot[a][b] = -s;
          }
        }

        result = rot * result;
      }
    }
  }

  // 3. Apply translation: last column
  for (uint32_t i = 0; i < Dim; ++i) {
    result[N - 1][i] = position_[i];
  }

  return result;
}

} // namespace window
