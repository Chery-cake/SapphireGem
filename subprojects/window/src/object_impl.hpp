#pragma once
#include "bindless_types.h"
#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include "shader_manager.h"
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
  computeUpdatePipeline_ = std::move(other.computeUpdatePipeline_);
  pipelineConfig_ = other.pipelineConfig_;
  time_ = other.time_;
  baseTextureId_ = other.baseTextureId_;
  faceMaterials_ = std::move(other.faceMaterials_);
  bindlessDescriptorSet_ = other.bindlessDescriptorSet_;
  other.bindlessDescriptorSet_ = vk::DescriptorSet{};
  uniformBuffers_ = std::move(other.uniformBuffers_);
  faceDataBuffers_ = std::move(other.faceDataBuffers_);
  positionBuffers_ = std::move(other.positionBuffers_);
  displacedPositionBuffers_ = std::move(other.displacedPositionBuffers_);
  indexBuffer_ = std::move(other.indexBuffer_);
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
    computeUpdatePipeline_ = std::move(other.computeUpdatePipeline_);
    pipelineConfig_ = other.pipelineConfig_;
    time_ = other.time_;
    baseTextureId_ = other.baseTextureId_;
    faceMaterials_ = std::move(other.faceMaterials_);
    positionBuffers_ = std::move(other.positionBuffers_);
    displacedPositionBuffers_ = std::move(other.displacedPositionBuffers_);
    indexBuffer_ = std::move(other.indexBuffer_);
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

  if (baseMaterialTag_ == nullptr) {
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
  std::println("  [2] Base position SSBO (static)");
  std::println("  [3] Displaced position SSBO (per-frame, compute-written)");
  std::println("  [4] Index SSBO (topology, static)");

  // Create descriptor set layout (owned by this object)
  // Binding 0: UBO (uniform buffer)
  // Binding 1: FaceData SSBO (per-face materials)
  // Binding 2: Base VertexPosition SSBO (static undeformed positions)
  // Binding 3: Displaced VertexPosition SSBO (per-frame, compute-written)
  // Binding 4: Index buffer as SSBO (topology data for compute adjacency)
  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  bindings.reserve(5);

  vk::DescriptorSetLayoutBinding uboLayoutBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
          vk::ShaderStageFlagBits::eCompute};
  bindings.push_back(uboLayoutBinding);

  vk::DescriptorSetLayoutBinding faceDataLayoutBinding{
      1, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute};
  bindings.push_back(faceDataLayoutBinding);

  vk::DescriptorSetLayoutBinding basePositionLayoutBinding{
      2, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eCompute};
  bindings.push_back(basePositionLayoutBinding);

  vk::DescriptorSetLayoutBinding displacedPositionLayoutBinding{
      3, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute};
  bindings.push_back(displacedPositionLayoutBinding);

  vk::DescriptorSetLayoutBinding indexLayoutBinding{
      4, vk::DescriptorType::eStorageBuffer, 1,
      vk::ShaderStageFlagBits::eCompute};
  bindings.push_back(indexLayoutBinding);

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

  // Create vertex position storage buffers (one per frame in flight)
  // These hold the STATIC base positions (undeformed) — compute shader reads
  // them.
  const size_t vertCount = std::max(vertices_.size(), static_cast<size_t>(1));
  const vk::DeviceSize positionDataSize =
      vertCount * sizeof(device::GPUVertexPosition);
  positionBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ssbo = allocator.createHostVisibleStorageBuffer(
        positionDataSize,
        std::string(name_) + "_basePositions_" + std::to_string(i));
    if (!ssbo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create position buffer {} for '{}'", i,
                   name_);
      release();
      return false;
    }
    positionBuffers_.push_back(std::move(ssbo));
  }

  // Create displaced position storage buffers (one per frame in flight)
  // These are written by the compute shader each frame with displaced
  // positions.
  displacedPositionBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ssbo = allocator.createHostVisibleStorageBuffer(
        positionDataSize,
        std::string(name_) + "_displaced_" + std::to_string(i));
    if (!ssbo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create displaced position buffer {} for "
                   "'{}'",
                   i, name_);
      release();
      return false;
    }
    displacedPositionBuffers_.push_back(std::move(ssbo));
  }

  // Create index+storage dual-use buffer (static topology data)
  {
    const size_t idxCount = std::max(indices_.size(), static_cast<size_t>(1));
    const vk::DeviceSize indexDataSize = idxCount * sizeof(uint32_t);
    indexBuffer_ = allocator.createIndexStorageBuffer(
        indexDataSize, std::string(name_) + "_indices");
    if (!indexBuffer_.isValid()) {
      std::println(stderr, "[Object] Failed to create index buffer for '{}'",
                   name_);
      release();
      return false;
    }
    // Upload index data
    void *idxMapped = indexBuffer_.map();
    if (idxMapped) {
      std::memcpy(idxMapped, indices_.data(),
                  indices_.size() * sizeof(uint32_t));
      indexBuffer_.unmap();
    }
  }

  // Upload vertex positions and colors to all base position buffers
  // No wave-flag computation — effects are handled entirely by the compute
  // shader.
  {
    std::vector<device::GPUVertexPosition> gpuPositions(vertCount);

    for (size_t vi = 0; vi < vertices_.size(); ++vi) {
      device::GPUVertexPosition gp;
      if constexpr (Dim >= 1)
        gp.x = vertices_[vi].position[0];
      if constexpr (Dim >= 2)
        gp.y = vertices_[vi].position[1];
      if constexpr (Dim >= 3)
        gp.z = vertices_[vi].position[2];
      gp.w = 1.0f;
      gp.r = vertices_[vi].color[0];
      gp.g = vertices_[vi].color[1];
      gp.b = vertices_[vi].color[2];
      gp.pad0 = 0.0f;
      // Default normal (up). Overwritten by object_compute.slang at load
      // time with area-weighted smooth normals from the actual mesh
      // topology.
      gp.nx = 0.0f;
      gp.ny = 1.0f;
      gp.nz = 0.0f;
      gp.npad = 0.0f;
      gpuPositions[vi] = gp;
    }

    for (uint32_t i = 0; i < framesInFlight; ++i) {
      void *mapped = positionBuffers_[i].map();
      if (mapped) {
        std::memcpy(mapped, gpuPositions.data(),
                    vertCount * sizeof(device::GPUVertexPosition));
        positionBuffers_[i].unmap();
      }
      // Also initialize displaced positions to base positions
      void *dispMapped = displacedPositionBuffers_[i].map();
      if (dispMapped) {
        std::memcpy(dispMapped, gpuPositions.data(),
                    vertCount * sizeof(device::GPUVertexPosition));
        displacedPositionBuffers_[i].unmap();
      }
    }
  }

  // Create descriptor pool with room for UBO + 4 SSBOs per frame
  std::vector<vk::DescriptorPoolSize> poolSizes;
  poolSizes.emplace_back(vk::DescriptorType::eUniformBuffer, framesInFlight);
  poolSizes.emplace_back(
      vk::DescriptorType::eStorageBuffer,
      framesInFlight * 4); // face data + base pos + displaced pos + indices

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

  // Update descriptor sets to point to uniform buffers, face data SSBOs,
  // base position SSBOs, displaced position SSBOs, and index SSBO
  const vk::DeviceSize indexDataSize =
      std::max(indices_.size(), static_cast<size_t>(1)) * sizeof(uint32_t);
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

    vk::DescriptorBufferInfo basePositionInfo{positionBuffers_[i].getBuffer(),
                                              0, positionDataSize};

    vk::WriteDescriptorSet basePositionWrite{*descriptorSets_[i],
                                             2,
                                             0,
                                             1,
                                             vk::DescriptorType::eStorageBuffer,
                                             nullptr,
                                             &basePositionInfo,
                                             nullptr};

    vk::DescriptorBufferInfo displacedPositionInfo{
        displacedPositionBuffers_[i].getBuffer(), 0, positionDataSize};

    vk::WriteDescriptorSet displacedPositionWrite{
        *descriptorSets_[i],
        3,
        0,
        1,
        vk::DescriptorType::eStorageBuffer,
        nullptr,
        &displacedPositionInfo,
        nullptr};

    vk::DescriptorBufferInfo indexInfo{indexBuffer_.getBuffer(), 0,
                                       indexDataSize};

    vk::WriteDescriptorSet indexWrite{*descriptorSets_[i],
                                      4,
                                      0,
                                      1,
                                      vk::DescriptorType::eStorageBuffer,
                                      nullptr,
                                      &indexInfo,
                                      nullptr};

    device.getRaiiDevice().updateDescriptorSets(
        {uboWrite, faceDataWrite, basePositionWrite, displacedPositionWrite,
         indexWrite},
        {});
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
  positionBuffers_.clear();
  displacedPositionBuffers_.clear();
  indexBuffer_ = {};
  computeUpdatePipeline_.reset();
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
void Object<Dim>::uploadFaceData(uint32_t frameIndex) const {
  if (!initialized_ || faceDataBuffers_.empty()) {
    return;
  }

  if (frameIndex >= faceDataBuffers_.size()) {
    return;
  }

  // Build GPUFaceData array from faceMaterials_
  const size_t faceCount = std::max(faces_.size(), static_cast<size_t>(1));
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

  // Upload face data SSBO for this frame (cheap memcpy to persistently mapped
  // buffer)
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

  // Push constants: time + objectId + vertexCount + indexCount
  if (pipelineConfig_.pushConstantSize >=
      sizeof(device::BindlessPushConstants)) {
    device::BindlessPushConstants pushData(
        time_, baseTextureId_.index, static_cast<uint32_t>(vertices_.size()),
        static_cast<uint32_t>(indices_.size()));
    cmd.pushConstants(**objectPipeline_.pipelineLayout,
                      pipelineConfig_.pushConstantStages, 0,
                      sizeof(device::BindlessPushConstants), &pushData);
  }

  // Indexed draw call — uses the dual-use index+storage buffer
  if (!indices_.empty()) {
    cmd.bindIndexBuffer(indexBuffer_.getBuffer(), 0, vk::IndexType::eUint32);
    cmd.drawIndexed(static_cast<uint32_t>(indices_.size()), 1, 0, 0, 0);
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

// ============================================================================
// Compute pipeline initialization
// ============================================================================

template <uint32_t Dim>
bool Object<Dim>::initializeCompute(device::GPUDevice &device,
                                    device::ShaderManager &shaderManager,
                                    const device::ShaderTag *computeUpdateTag,
                                    const device::ShaderTag *computeNormalTag) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_ || !objectPipeline_.isValid()) {
    std::println(
        stderr,
        "[Object] initializeCompute: '{}' must be initialized first", name_);
    return false;
  }

  // The compute pipelines share the same pipeline layout as the graphics
  // pipeline (same set 0 descriptor layout, same push constant range).
  vk::PipelineLayout sharedLayout = **objectPipeline_.pipelineLayout;

  // ---- Create per-frame update compute pipeline (object_update.slang) ----
  if (computeUpdateTag) {
    auto result = shaderManager.acquire(computeUpdateTag);
    if (!result.has_value() || !result.value() ||
        !result.value()->compute || !result.value()->compute->isValid) {
      std::println(stderr,
                   "[Object] Failed to acquire compute update shader for '{}'",
                   name_);
    } else {
      device::ShaderProgram *prog = result.value();
      vk::PipelineShaderStageCreateInfo stageInfo =
          prog->compute->getStageInfo();
      vk::ComputePipelineCreateInfo pipelineInfo{{}, stageInfo, sharedLayout};
      try {
        computeUpdatePipeline_ = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr, pipelineInfo);
        std::println("[Object] Created compute update pipeline for '{}'",
                     name_);
      } catch (const vk::SystemError &e) {
        std::println(
            stderr,
            "[Object] Failed to create compute update pipeline for '{}': {}",
            name_, e.what());
      }
      shaderManager.release(computeUpdateTag);
    }
  }

  // ---- One-shot normal precomputation dispatch (object_compute.slang) ----
  if (computeNormalTag && !vertices_.empty()) {
    auto result = shaderManager.acquire(computeNormalTag);
    if (!result.has_value() || !result.value() ||
        !result.value()->compute || !result.value()->compute->isValid) {
      std::println(stderr,
                   "[Object] Failed to acquire compute normal shader for '{}'",
                   name_);
    } else {
      device::ShaderProgram *prog = result.value();
      vk::PipelineShaderStageCreateInfo stageInfo =
          prog->compute->getStageInfo();
      vk::ComputePipelineCreateInfo pipelineInfo{{}, stageInfo, sharedLayout};

      std::unique_ptr<vk::raii::Pipeline> normalPipeline;
      try {
        normalPipeline = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr, pipelineInfo);
      } catch (const vk::SystemError &e) {
        std::println(
            stderr,
            "[Object] Failed to create normal compute pipeline for '{}': {}",
            name_, e.what());
      }

      if (normalPipeline) {
        // One-shot command buffer on the graphics queue
        auto qf = device.getQueueFamilies();
        uint32_t queueFamily =
            qf.graphicsFamily.value_or(qf.computeFamily.value_or(0u));

        try {
          vk::CommandPoolCreateInfo poolInfo{
              vk::CommandPoolCreateFlagBits::eTransient, queueFamily};
          vk::raii::CommandPool cmdPool(device.getRaiiDevice(), poolInfo);

          vk::CommandBufferAllocateInfo cmdAlloc{
              *cmdPool, vk::CommandBufferLevel::ePrimary, 1};
          auto cmdBufs =
              vk::raii::CommandBuffers(device.getRaiiDevice(), cmdAlloc);
          auto &cmd = cmdBufs[0];

          cmd.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
          cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **normalPipeline);
          cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, sharedLayout,
                                 0, {*descriptorSets_[0]}, {});
          if (bindlessDescriptorSet_) {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                   sharedLayout, 1,
                                   {bindlessDescriptorSet_}, {});
          }

          device::BindlessPushConstants pushData(
              0.0f, baseTextureId_.index,
              static_cast<uint32_t>(vertices_.size()),
              static_cast<uint32_t>(indices_.size()));
          cmd.pushConstants(sharedLayout, pipelineConfig_.pushConstantStages,
                            0u,
                            vk::ArrayProxy<const device::BindlessPushConstants>(
                                pushData));

          uint32_t groups =
              (static_cast<uint32_t>(vertices_.size()) + 63u) / 64u;
          cmd.dispatch(groups, 1, 1);
          cmd.end();

          vk::SubmitInfo submit{};
          submit.setCommandBuffers(*cmd);
          device.getGraphicsQueue().submit(submit);
          device.getGraphicsQueue().waitIdle();
          std::println(
              "[Object] Normal precomputation dispatched for '{}'", name_);
        } catch (const vk::SystemError &e) {
          std::println(
              stderr,
              "[Object] Failed to dispatch normal precomputation for '{}': {}",
              name_, e.what());
        }
      }

      shaderManager.release(computeNormalTag);
    }
  }

  return true;
}

// ============================================================================
// Pre-render compute dispatch (wave displacement)
// ============================================================================

template <uint32_t Dim>
void Object<Dim>::preRender(vk::CommandBuffer cmd,
                            uint32_t frameIndex) const {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_ || !computeUpdatePipeline_) {
    return;
  }

  if (frameIndex >= descriptorSets_.size()) {
    return;
  }

  // Dispatch wave displacement compute shader
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computeUpdatePipeline_);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         **objectPipeline_.pipelineLayout, 0,
                         {*descriptorSets_[frameIndex]}, {});
  if (bindlessDescriptorSet_) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **objectPipeline_.pipelineLayout, 1,
                           {bindlessDescriptorSet_}, {});
  }

  device::BindlessPushConstants pushData(
      time_, baseTextureId_.index,
      static_cast<uint32_t>(vertices_.size()),
      static_cast<uint32_t>(indices_.size()));
  cmd.pushConstants(**objectPipeline_.pipelineLayout,
                    pipelineConfig_.pushConstantStages, 0,
                    sizeof(device::BindlessPushConstants), &pushData);

  uint32_t groups =
      (static_cast<uint32_t>(vertices_.size()) + 63u) / 64u;
  cmd.dispatch(groups, 1, 1);

  // Pipeline barrier: ensure compute writes to displacedPositions are visible
  // to the subsequent vertex shader reads.
  vk::BufferMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = displacedPositionBuffers_[frameIndex].getBuffer();
  barrier.offset = 0;
  barrier.size = VK_WHOLE_SIZE;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eVertexShader, {}, {},
                      {barrier}, {});
}

} // namespace window
