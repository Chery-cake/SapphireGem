#pragma once
#include "object.h"

namespace window {

template <uint32_t Dim>
Object<Dim>::Object(const ObjectTag &tag)
    : name_(tag.name), materialTag_(tag.materialTag) {}

template <uint32_t Dim> Object<Dim>::~Object() { release(); }

template <uint32_t Dim> Object<Dim>::Object(Object &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.objectMutex_);
  name_ = std::move(other.name_);
  materialTag_ = other.materialTag_;
  other.materialTag_ = nullptr;
  transform_ = other.transform_;
  uniformBuffers_ = std::move(other.uniformBuffers_);
  descriptorPool_ = std::move(other.descriptorPool_);
  descriptorSets_ = std::move(other.descriptorSets_);
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
    uniformBuffers_ = std::move(other.uniformBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

template <uint32_t Dim>
bool Object<Dim>::initialize(device::VMAAllocator &allocator,
                             device::GPUDevice &device,
                             vk::DescriptorSetLayout descriptorSetLayout,
                             uint32_t framesInFlight) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (initialized_) { // TODO see if need a warning print
    return false;     // Already initialized - call release() first to
                      // reinitialize
  }

  // Create uniform buffers (one per frame in flight)
  uniformBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ubo = allocator.createUniformBuffer(sizeof(UniformBufferData),
                                             std::string(name_) + "_ubo_" +
                                                 std::to_string(i));
    if (!ubo.isValid()) {
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
  } catch (const vk::SystemError &) {
    return false;
  }

  // Allocate descriptor sets
  std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                               descriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo{**descriptorPool_, framesInFlight,
                                          layouts.data()};

  try {
    auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(), allocInfo);
    descriptorSets_.reserve(sets.size());
    for (auto &s : sets) {
      descriptorSets_.push_back(std::move(s));
    }
  } catch (const vk::SystemError &) {
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
  return true;
}

template <uint32_t Dim> void Object<Dim>::release() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  uniformBuffers_.clear();
  initialized_ = false;
}

template <uint32_t Dim>
void Object<Dim>::updateUniforms(uint32_t frameIndex, const float *viewMatrix,
                                 const float *projMatrix) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_ || frameIndex >= uniformBuffers_.size()) {
    return;
  }

  UniformBufferData uboData{};
  buildModelMatrix(uboData.model);

  if (viewMatrix) {
    std::memcpy(uboData.view, viewMatrix, sizeof(float) * 16);
  } else {
    // Identity matrix
    setIdentity(uboData.view);
  }

  if (projMatrix) {
    std::memcpy(uboData.proj, projMatrix, sizeof(float) * 16);
  } else {
    setIdentity(uboData.proj);
  }

  void *mapped = uniformBuffers_[frameIndex].map();
  if (mapped) {
    std::memcpy(mapped, &uboData, sizeof(UniformBufferData));
    uniformBuffers_[frameIndex].unmap();
  }
}

template <uint32_t Dim>
void Object<Dim>::draw(vk::CommandBuffer cmd, const Material &material,
                       uint32_t frameIndex, uint32_t vertexCount,
                       uint32_t instanceCount) const {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_ || frameIndex >= descriptorSets_.size()) {
    return;
  }

  // Bind material pipeline
  material.bind(cmd);

  // Bind descriptor set for this frame
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         material.getPipelineLayout(), 0,
                         {*descriptorSets_[frameIndex]}, {});

  // Draw
  cmd.draw(vertexCount, instanceCount, 0, 0);
}

} // namespace window
