#pragma once
#include "object.h"
#include <print>

namespace window {

template <uint32_t Dim>
Object<Dim>::Object(const ObjectTag &tag)
    : name_(tag.name), materialTag_(tag.materialTag) {
  if (tag.dimension != Dim) {
    std::println(stderr,
                 "[Object] Warning: ObjectTag dimension ({}) does not match "
                 "template dimension ({}) for '{}'",
                 tag.dimension, Dim, tag.name);
  }
}

template <uint32_t Dim> Object<Dim>::~Object() { release(); }

template <uint32_t Dim> Object<Dim>::Object(Object &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.objectMutex_);
  name_ = std::move(other.name_);
  materialTag_ = other.materialTag_;
  other.materialTag_ = nullptr;
  transform_ = other.transform_;
  objectPipeline_ = std::move(other.objectPipeline_);
  uniformBuffers_ = std::move(other.uniformBuffers_);
  descriptorPool_ = std::move(other.descriptorPool_);
  descriptorSets_ = std::move(other.descriptorSets_);
  descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
  initialized_ = other.initialized_;
  other.initialized_ = false;
}

template <uint32_t Dim>
Object<Dim> &Object<Dim>::operator=(Object<Dim> &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(objectMutex_, other.objectMutex_);
    release();
    name_ = std::move(other.name_);
    materialTag_ = other.materialTag_;
    other.materialTag_ = nullptr;
    transform_ = other.transform_;
    objectPipeline_ = std::move(other.objectPipeline_);
    uniformBuffers_ = std::move(other.uniformBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

template <uint32_t Dim>
bool Object<Dim>::initialize(device::VMAAllocator &allocator,
                             device::GPUDevice &device, Material &material,
                             vk::RenderPass renderPass,
                             uint32_t framesInFlight,
                             const PipelineConfig &pipelineConfig) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (initialized_) {
    std::println(stderr,
                 "[Object] Already initialized: {} - call release() first",
                 name_);
    return false;
  }

  // Validate parameters
  if (!materialTag_) {
    std::println(stderr, "[Object] No material tag set for object: {}", name_);
    return false;
  }

  if (!material.isInitialized()) {
    std::println(stderr,
                 "[Object] Material '{}' not initialized for object: {}",
                 material.getName(), name_);
    return false;
  }

  if (framesInFlight == 0) {
    std::println(stderr,
                 "[Object] framesInFlight must be > 0 for object: {}", name_);
    return false;
  }

  // Create descriptor set layout (owned by this object)
  vk::DescriptorSetLayoutBinding uboLayoutBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};

  vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{{}, 1, &uboLayoutBinding};

  try {
    descriptorSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutCreateInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor set layout for '{}': {}",
                 name_, e.what());
    return false;
  }

  // Create per-object pipeline from material
  objectPipeline_ = material.createPipelineForObject(
      device, renderPass, **descriptorSetLayout_, pipelineConfig);
  if (!objectPipeline_.isValid()) {
    std::println(stderr,
                 "[Object] Failed to create pipeline for object: {}", name_);
    descriptorSetLayout_.reset();
    return false;
  }

  // Create uniform buffers (one per frame in flight)
  uniformBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ubo = allocator.createUniformBuffer(sizeof(UniformBufferData),
                                             std::string(name_) + "_ubo_" +
                                                 std::to_string(i));
    if (!ubo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create uniform buffer {} for '{}'", i,
                   name_);
      release();
      return false;
    }
    uniformBuffers_.push_back(std::move(ubo));
  }

  // Create descriptor pool
  vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer,
                                  framesInFlight};

  vk::DescriptorPoolCreateInfo poolInfo{
      vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, framesInFlight, 1,
      &poolSize};

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

  // Update descriptor sets to point to uniform buffers
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    vk::DescriptorBufferInfo bufferInfo{uniformBuffers_[i].getBuffer(), 0,
                                        sizeof(UniformBufferData)};

    vk::WriteDescriptorSet descriptorWrite{*descriptorSets_[i],
                                           0,
                                           0,
                                           1,
                                           vk::DescriptorType::eUniformBuffer,
                                           nullptr,
                                           &bufferInfo,
                                           nullptr};

    device.getRaiiDevice().updateDescriptorSets(descriptorWrite, {});
  }

  initialized_ = true;
  std::println("[Object] Initialized '{}' ({}D, {} frames)", name_, Dim,
               framesInFlight);
  return true;
}

template <uint32_t Dim> void Object<Dim>::release() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  uniformBuffers_.clear();
  objectPipeline_.reset();
  descriptorSetLayout_.reset();
  initialized_ = false;
}

template <uint32_t Dim>
void Object<Dim>::updateUniforms(uint32_t frameIndex,
                                 const glm::mat4 &viewMatrix,
                                 const glm::mat4 &projMatrix) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_) {
    return;
  }

  if (frameIndex >= uniformBuffers_.size()) {
    std::println(stderr,
                 "[Object] Invalid frame index {} (max {}) for '{}'",
                 frameIndex, uniformBuffers_.size() - 1, name_);
    return;
  }

  UniformBufferData uboData{};
  uboData.model = buildModelMatrix();
  uboData.view = viewMatrix;
  uboData.proj = projMatrix;

  void *mapped = uniformBuffers_[frameIndex].map();
  if (mapped) {
    std::memcpy(mapped, &uboData, sizeof(UniformBufferData));
    uniformBuffers_[frameIndex].unmap();
  }
}

template <uint32_t Dim>
void Object<Dim>::draw(vk::CommandBuffer cmd, uint32_t frameIndex,
                       uint32_t vertexCount, uint32_t instanceCount) const {
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

  if (vertexCount == 0) {
    return;
  }

  // Bind per-object pipeline
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                   **objectPipeline_.pipeline);

  // Bind per-object descriptor set for this frame
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         **objectPipeline_.pipelineLayout, 0,
                         {*descriptorSets_[frameIndex]}, {});

  // Draw
  cmd.draw(vertexCount, instanceCount, 0, 0);
}

/**
 * @brief Build a 4x4 model matrix from the dimension-agnostic transform
 *
 * Works for any dimension by mapping position/rotation/scale to glm::mat4.
 * For Dim >= 3, uses full 3D transforms.
 * For Dim == 2, rotation is around the Z axis only.
 * For Dim == 1, only translation along X is applied.
 * For Dim > 3, higher dimensions are ignored in the 4x4 matrix.
 */
template <uint32_t Dim> glm::mat4 Object<Dim>::buildModelMatrix() const {
  glm::mat4 model(1.0f);

  // Extract position (up to 3 components)
  glm::vec3 pos(0.0f);
  if constexpr (Dim >= 1)
    pos.x = transform_.position[0];
  if constexpr (Dim >= 2)
    pos.y = transform_.position[1];
  if constexpr (Dim >= 3)
    pos.z = transform_.position[2];

  // Extract scale (up to 3 components)
  glm::vec3 scl(1.0f);
  if constexpr (Dim >= 1)
    scl.x = transform_.scale[0];
  if constexpr (Dim >= 2)
    scl.y = transform_.scale[1];
  if constexpr (Dim >= 3)
    scl.z = transform_.scale[2];

  // Apply transformations: T * Rz * Ry * Rx * S
  model = glm::translate(model, pos);

  // Apply rotations
  if constexpr (Dim >= 3) {
    // Full 3D rotation: Z, then Y, then X (Euler angles)
    model = glm::rotate(model, transform_.rotation[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, transform_.rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, transform_.rotation[0], glm::vec3(1.0f, 0.0f, 0.0f));
  } else if constexpr (Dim == 2) {
    // 2D rotation: around Z axis only
    model = glm::rotate(model, transform_.rotation[0], glm::vec3(0.0f, 0.0f, 1.0f));
    // Second rotation component (if present) for 2D shear/secondary axis
    if constexpr (Dim >= 2) {
      model = glm::rotate(model, transform_.rotation[1], glm::vec3(0.0f, 1.0f, 0.0f));
    }
  } else if constexpr (Dim == 1) {
    // 1D: rotation around Z axis
    model = glm::rotate(model, transform_.rotation[0], glm::vec3(0.0f, 0.0f, 1.0f));
  }

  model = glm::scale(model, scl);

  return model;
}

} // namespace window
