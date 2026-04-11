#pragma once
#include "bindless_types.h"
#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include "vulkan/vulkan.hpp"
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

  // Initialize per-face material array
  faceMaterials_.resize(faces_.size());
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
  objectPipeline_ = std::move(other.objectPipeline_);
  pipelineConfig_ = other.pipelineConfig_;
  time_ = other.time_;
  baseTextureId_ = other.baseTextureId_;
  faceMaterials_ = std::move(other.faceMaterials_);
  bindlessDescriptorSet_ = other.bindlessDescriptorSet_;
  other.bindlessDescriptorSet_ = vk::DescriptorSet{};
  uniformBuffers_ = std::move(other.uniformBuffers_);
  faceDataBuffers_ = std::move(other.faceDataBuffers_);
  descriptorPool_ = std::move(other.descriptorPool_);
  descriptorSets_ = std::move(other.descriptorSets_);
  descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
  initialized_ = other.initialized_;
  geometryDirty_ = other.geometryDirty_;
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
    objectPipeline_ = std::move(other.objectPipeline_);
    pipelineConfig_ = other.pipelineConfig_;
    time_ = other.time_;
    baseTextureId_ = other.baseTextureId_;
    faceMaterials_ = std::move(other.faceMaterials_);
    bindlessDescriptorSet_ = other.bindlessDescriptorSet_;
    other.bindlessDescriptorSet_ = vk::DescriptorSet{};
    uniformBuffers_ = std::move(other.uniformBuffers_);
    faceDataBuffers_ = std::move(other.faceDataBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    geometryDirty_ = other.geometryDirty_;
    other.initialized_ = false;
  }
  return *this;
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
                             const PipelineConfig &pipelineConfig,
                             vk::DescriptorSetLayout bindlessSetLayout) {
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

  // Log binding table for this object's pipeline
  std::println("[Object] Binding table for '{}':", name_);
  std::println("  [0] UBO ({}D, {}B)", Dim, sizeof(GPUUBO));
  std::println("  [1] FaceData SSBO ({} faces, {}B)", faces_.size(),
               faces_.size() * sizeof(device::GPUFaceData));

  // Create descriptor set layout (owned by this object)
  // Binding 0: UBO (uniform buffer) — textures are accessed via bindless (set
  // 1)
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  bindings.reserve(2);

  vk::DescriptorSetLayoutBinding uboLayoutBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};
  bindings.push_back(uboLayoutBinding);

  vk::DescriptorSetLayoutBinding faceDataLayoutBinding{
      1, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};
  bindings.push_back(faceDataLayoutBinding);

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

  // Create per-object pipeline from base material
  objectPipeline_ = baseMaterial.createPipelineForObject(
      device, renderPass, **descriptorSetLayout_, pipelineConfig, 0,
      bindlessSetLayout);
  if (!objectPipeline_.isValid()) {
    std::println(stderr, "[Object] Failed to create pipeline for object: {}",
                 name_);
    descriptorSetLayout_.reset();
    return false;
  }

  // Store pipeline config for push constants during draw
  pipelineConfig_ = pipelineConfig;

  // Create uniform buffers (one per frame in flight)
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

  // Create face data storage buffers (one per frame in flight)
  // Host-visible (CpuToGpu + persistently mapped) so the CPU can write
  // per-face data directly every frame without staging.
  const vk::DeviceSize faceDataSize =
      std::max(faces_.size(), static_cast<size_t>(1)) *
      sizeof(device::GPUFaceData);
  faceDataBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ssbo = allocator.createHostVisibleStorageBuffer(
        faceDataSize, std::string(name_) + "_facedata_" + std::to_string(i));
    if (!ssbo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create face data buffer {} for '{}'", i,
                   name_);
      release();
      return false;
    }
    faceDataBuffers_.push_back(std::move(ssbo));
  }

  // Create descriptor pool with room for UBO + face data SSBO
  std::vector<vk::DescriptorPoolSize> poolSizes;
  poolSizes.push_back({vk::DescriptorType::eUniformBuffer, framesInFlight});
  poolSizes.push_back({vk::DescriptorType::eStorageBuffer, framesInFlight});

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

  // Update descriptor sets to point to uniform buffers and face data SSBOs
  for (uint32_t i = 0; i < framesInFlight; ++i) {
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

    vk::DescriptorBufferInfo faceDataInfo{faceDataBuffers_[i].getBuffer(), 0,
                                          faceDataSize};

    vk::WriteDescriptorSet faceDataWrite{*descriptorSets_[i],
                                         1,
                                         0,
                                         1,
                                         vk::DescriptorType::eStorageBuffer,
                                         nullptr,
                                         &faceDataInfo,
                                         nullptr};

    device.getRaiiDevice().updateDescriptorSets({uboWrite, faceDataWrite}, {});
  }

  // Upload initial face data to all frames
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    uploadFaceData(i);
  }

  initialized_ = true;
  std::println("[Object] Initialized '{}' ({}D, {} faces, {} frames, UBO={}B)",
               name_, Dim, faces_.size(), framesInFlight, uboSize);
  return true;
}

template <uint32_t Dim> void Object<Dim>::release() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  uniformBuffers_.clear();
  faceDataBuffers_.clear();
  objectPipeline_.reset();
  descriptorSetLayout_.reset();
  bindlessDescriptorSet_ = vk::DescriptorSet{};
  geometryDirty_ = true;
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
// Bindless texture ID support
// ============================================================================

template <uint32_t Dim> void Object<Dim>::setTextureId(device::TextureId id) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  baseTextureId_ = id;
}

template <uint32_t Dim> device::TextureId Object<Dim>::getTextureId() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return baseTextureId_;
}

template <uint32_t Dim>
void Object<Dim>::setBindlessDescriptorSet(vk::DescriptorSet set) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  bindlessDescriptorSet_ = set;
}

// ============================================================================
// Per-face material system
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::setFaceMaterial(uint32_t faceIndex,
                                  const device::FaceMaterial &desc) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  // Validate face index against actual face count when faces are known
  if (!faces_.empty() && faceIndex >= static_cast<uint32_t>(faces_.size())) {
    std::println(stderr,
                 "[Object] setFaceMaterial: face index {} out of range "
                 "(object '{}' has {} faces)",
                 faceIndex, name_, faces_.size());
    return;
  }

  // When faces_ is empty (pre-init), allow but cap to prevent unbounded
  // growth
  if (faces_.empty() && faceIndex > 1024) {
    std::println(stderr,
                 "[Object] setFaceMaterial: face index {} too large "
                 "(object '{}' has no faces yet)",
                 faceIndex, name_);
    return;
  }

  if (faceMaterials_.size() <= faceIndex) {
    faceMaterials_.resize(faceIndex + 1);
  }
  faceMaterials_[faceIndex] = desc;
}

template <uint32_t Dim>
device::FaceMaterial Object<Dim>::getFaceMaterial(uint32_t faceIndex) const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (faceIndex < static_cast<uint32_t>(faceMaterials_.size())) {
    return faceMaterials_[faceIndex];
  }
  return {};
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
// Face data SSBO upload
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::uploadFaceData(uint32_t frameIndex) {
  if (!initialized_ || faceDataBuffers_.empty()) {
    return;
  }

  if (frameIndex >= faceDataBuffers_.size()) {
    return;
  }

  // Build GPUFaceData array from faceMaterials_
  const size_t faceCount =
      std::max(faces_.size(), static_cast<size_t>(1));
  std::vector<device::GPUFaceData> gpuData(faceCount);

  for (size_t i = 0; i < faceCount; ++i) {
    if (i < faceMaterials_.size()) {
      gpuData[i] = device::GPUFaceData::fromFaceMaterial(faceMaterials_[i]);
    } else {
      gpuData[i] = device::GPUFaceData::fromFaceMaterial({});
    }
  }

  void *mapped = faceDataBuffers_[frameIndex].map();
  if (mapped) {
    std::memcpy(mapped, gpuData.data(),
                faceCount * sizeof(device::GPUFaceData));
    faceDataBuffers_[frameIndex].unmap();
  }
}

// ============================================================================
// Draw
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::draw(vk::CommandBuffer cmd, uint32_t frameIndex) {
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

  // Upload face data SSBO for this frame (cheap memcpy to persistently mapped buffer)
  uploadFaceData(frameIndex);

  // Bind the single pipeline and descriptor sets
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                   **objectPipeline_.pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         **objectPipeline_.pipelineLayout, 0,
                         {*descriptorSets_[frameIndex]}, {});

  // Push time constant if configured
  // Bind global bindless descriptor set (set 1) if available
  if (bindlessDescriptorSet_) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           **objectPipeline_.pipelineLayout, 1,
                           {bindlessDescriptorSet_}, {});
  }

  // Push constants: time + objectId (bindless)
  if (pipelineConfig_.pushConstantSize >=
      sizeof(device::BindlessPushConstants)) {
    device::BindlessPushConstants pushData(time_, baseTextureId_.index);
    cmd.pushConstants(**objectPipeline_.pipelineLayout,
                      pipelineConfig_.pushConstantStages, 0,
                      sizeof(device::BindlessPushConstants), &pushData);
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
