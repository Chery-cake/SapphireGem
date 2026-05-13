#pragma once
#include "bindless_types.h"
#include "glm/ext/matrix_transform.hpp"
#include "glm/glm.hpp"
#include "material.h"
#include "object.h"
#include "pipeline_cache.h"
#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <utility>
#include <vk_mem_alloc_enums.hpp>

namespace ecs::component::object {

// ============================================================================
// Transform Component
// ============================================================================

template <uint32_t Dim>
typename TransformComponent<Dim>::MatType
TransformComponent<Dim>::modelMatrix() const {
  constexpr uint32_t N = Dim + 1; // Matrix dimension
  MatType result(1.0F);           // Identity

  // 1. Apply scale: multiply diagonal by scale factors

  std::ranges::for_each(std::views::iota(0U, Dim),
                        [&](uint32_t i) { result[i][i] = scale[i]; });

  // 2. Apply rotations in each axis-pair plane
  std::ranges::for_each(std::views::iota(0U, Dim), [&](uint32_t i) {
    float angle = rotation[i];
    if (angle == 0.0F) {
      return;
    }

    float c = std::cos(angle);
    float s = std::sin(angle);
    const uint32_t a = i;
    const uint32_t b = (i + 1) % Dim;
    MatType rot(1.0F);

    rot[a][a] = c;
    rot[a][b] = -s;

    rot[b][a] = s;
    rot[b][b] = c;

    result *= rot;
  });

  // 3. Apply translation: last column
  std::ranges::for_each(std::views::iota(0U, Dim),
                        [&](uint32_t i) { result[N - 1][i] = position[i]; });

  return result;
}

// ============================================================================
// Non-templated RenderComponent
// ============================================================================

bool RenderComponent::initialize(device::VMAAllocator &allocator,
                                 device::GPUDevice &device,
                                 const Mesh &mesh,
                                 const window::Material &baseMaterial,
                                 vk::RenderPass renderPass,
                                 uint32_t framesInFlight,
                                 const window::PipelineConfig &config,
                                 vk::DescriptorSetLayout bindlessLayout) {
  std::lock_guard lock(mutex_);

  if (initialized) {
    return false;
  }
  if (!mesh.gpuUploaded) {
    return false;
  }

  mesh_       = &mesh;
  dimension_  = mesh.dimension;
  pipelineConfig = config;
  // Stamp dimension into config so the pipeline cache separates 2D/3D entries
  pipelineConfig.dimension = dimension_;

  const size_t vCount    = std::max<size_t>(mesh.vertexCount(), 1);
  const size_t faceCount = std::max<size_t>(mesh.getFaceCount(), 1);
  const size_t idxCount  = std::max<size_t>(mesh.indices.size(), 1);

  // --- Descriptor set layout ---
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      {0, vk::DescriptorType::eUniformBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute},
      {1, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute},
      {2, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute},
      {3, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex |
           vk::ShaderStageFlagBits::eCompute},
      {4, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute}};
  try {
    descriptorSetLayout = std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(),
        vk::DescriptorSetLayoutCreateInfo{{}, bindings});
  } catch (...) {
    return false;
  }

  // --- Pipeline (dimension stamped into config) ---
  pipeline = window::PipelineCache::instance().getOrCreate(
      device, renderPass, **descriptorSetLayout, pipelineConfig, 0,
      bindlessLayout, baseMaterial.getShaderProgram());
  if (!pipeline) {
    return false;
  }

  // --- UBOs (size depends on dimension) ---
  const vk::DeviceSize uboSize = (dimension_ == 2)
                                     ? sizeof(GPUUniformBufferData<2>)
                                     : sizeof(GPUUniformBufferData<3>);
  uniformBuffers.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto buf = allocator.createUniformBuffer(uboSize,
                                             "ubo_" + std::to_string(i));
    if (!buf.isValid()) {
      release();
      return false;
    }
    uniformBuffers.push_back(std::move(buf));
  }

  // --- Indirect draw ---
  indirectDrawCmd = vk::DrawIndexedIndirectCommand{
      static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0};
  device::BufferCreateInfo indirectInfo{};
  indirectInfo.size = sizeof(VkDrawIndexedIndirectCommand);
  indirectInfo.usage = vk::BufferUsageFlagBits::eIndirectBuffer |
                       vk::BufferUsageFlagBits::eStorageBuffer;
  indirectInfo.memoryUsage = vma::MemoryUsage::eCpuToGpu;
  indirectInfo.flags = vma::AllocationCreateFlagBits::eMapped;
  indirectInfo.debugName = "indirect";
  indirectDrawBuffer = allocator.createBuffer(indirectInfo);
  if (!indirectDrawBuffer.isValid()) {
    release();
    return false;
  }
  uploadIndirectCommand();

  // --- Face data SSBOs ---
  const vk::DeviceSize faceDataSize =
      faceCount * sizeof(device::GPUFaceData);
  faceMaterials.resize(faceCount);
  faceDataBuffers.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto buf = allocator.createHostVisibleStorageBuffer(
        faceDataSize, "facedata_" + std::to_string(i));
    if (!buf.isValid()) {
      release();
      return false;
    }
    faceDataBuffers.push_back(std::move(buf));
  }

  // --- Displaced position SSBOs ---
  const vk::DeviceSize posDataSize =
      vCount * sizeof(device::GPUVertexPosition);
  displacedPositionBuffers.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto buf = allocator.createHostVisibleStorageBuffer(
        posDataSize, "displaced_" + std::to_string(i));
    if (!buf.isValid()) {
      release();
      return false;
    }
    displacedPositionBuffers.push_back(std::move(buf));
  }

  // Initialise displaced positions from the static base buffer
  void *src = mesh.positionBuffer.map();
  if (src != nullptr) {
    for (uint32_t i = 0; i < framesInFlight; ++i) {
      void *dst = displacedPositionBuffers[i].map();
      if (dst) {
        std::memcpy(dst, src, posDataSize);
        displacedPositionBuffers[i].unmap();
        displacedPositionBuffers[i].flush(0, posDataSize);
      }
    }
    mesh.positionBuffer.unmap();
  }

  // --- Descriptor pool & sets ---
  std::vector<vk::DescriptorPoolSize> poolSizes = {
      {vk::DescriptorType::eUniformBuffer, framesInFlight},
      {vk::DescriptorType::eStorageBuffer, framesInFlight * 4}};
  descriptorPool = std::make_unique<vk::raii::DescriptorPool>(
      device.getRaiiDevice(),
      vk::DescriptorPoolCreateInfo{
          vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
          framesInFlight, poolSizes});

  std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                               **descriptorSetLayout);
  auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(),
                                       {**descriptorPool, layouts});
  descriptorSets.reserve(sets.size());
  for (auto &s : sets) {
    descriptorSets.push_back(std::move(s));
  }

  for (uint32_t i = 0; i < framesInFlight; ++i) {
    vk::DescriptorBufferInfo uniformBuf{uniformBuffers[i].getBuffer(), 0,
                                        uboSize};
    vk::DescriptorBufferInfo faceDataBuf{faceDataBuffers[i].getBuffer(), 0,
                                         faceDataSize};
    vk::DescriptorBufferInfo posBuf{mesh.positionBuffer.getBuffer(), 0,
                                    posDataSize};
    vk::DescriptorBufferInfo dispBuf{
        displacedPositionBuffers[i].getBuffer(), 0, posDataSize};
    vk::DescriptorBufferInfo idxBuf{mesh.indexBuffer.getBuffer(), 0,
                                    idxCount * sizeof(uint32_t)};

    std::vector<vk::WriteDescriptorSet> writes = {
        {*descriptorSets[i], 0, 0, 1, vk::DescriptorType::eUniformBuffer,
         nullptr, &uniformBuf},
        {*descriptorSets[i], 1, 0, 1, vk::DescriptorType::eStorageBuffer,
         nullptr, &faceDataBuf},
        {*descriptorSets[i], 2, 0, 1, vk::DescriptorType::eStorageBuffer,
         nullptr, &posBuf},
        {*descriptorSets[i], 3, 0, 1, vk::DescriptorType::eStorageBuffer,
         nullptr, &dispBuf},
        {*descriptorSets[i], 4, 0, 1, vk::DescriptorType::eStorageBuffer,
         nullptr, &idxBuf}};
    device.getRaiiDevice().updateDescriptorSets(writes, {});
  }

  for (uint32_t i = 0; i < framesInFlight; ++i) {
    uploadFaceData(i, faceCount);
  }

  initialized = true;
  return true;
}

void RenderComponent::release() {
  std::lock_guard lock(mutex_);
  descriptorSets.clear();
  descriptorPool.reset();
  uniformBuffers.clear();
  faceDataBuffers.clear();
  displacedPositionBuffers.clear();
  indirectDrawBuffer = {};
  computeUpdatePipeline.reset();
  pipeline.reset();
  descriptorSetLayout.reset();
  bindlessDescriptorSet = vk::DescriptorSet{};
  indirectCommandDirty  = true;
  initialized           = false;
}

void RenderComponent::uploadIndirectCommand() const {
  std::lock_guard lock(mutex_);
  void *mapped = indirectDrawBuffer.map();
  if (mapped != nullptr) {
    std::memcpy(mapped, &indirectDrawCmd,
                sizeof(vk::DrawIndexedIndirectCommand));
    indirectDrawBuffer.unmap();
    indirectDrawBuffer.flush(0, sizeof(vk::DrawIndexedIndirectCommand));
  }
  indirectCommandDirty = false;
}

void RenderComponent::setFaceMaterial(uint32_t faceIndex,
                                      const device::FaceMaterial &desc,
                                      size_t faceCount) {
  std::lock_guard lock(mutex_);
  if (faceIndex >= faceCount) {
    return;
  }
  if (faceMaterials.size() <= faceIndex) {
    faceMaterials.resize(faceIndex + 1);
  }
  faceMaterials[faceIndex] = desc;
}

device::FaceMaterial RenderComponent::getFaceMaterial(uint32_t faceIndex) const {
  std::lock_guard lock(mutex_);
  if (faceIndex < faceMaterials.size()) {
    return faceMaterials[faceIndex];
  }
  return {};
}

void RenderComponent::uploadFaceData(uint32_t frameIndex,
                                     size_t faceCount) const {
  std::lock_guard lock(mutex_);
  if (faceDataBuffers.empty() || frameIndex >= faceDataBuffers.size()) {
    return;
  }

  const size_t count = std::max(faceCount, size_t(1));
  std::vector<device::GPUFaceData> gpuData(count);
  for (size_t i = 0; i < count; ++i) {
    gpuData[i] = device::GPUFaceData::fromFaceMaterial(
        i < faceMaterials.size() ? faceMaterials[i] : device::FaceMaterial{});
  }

  void *mapped = faceDataBuffers[frameIndex].map();
  if (mapped != nullptr) {
    std::memcpy(mapped, gpuData.data(), count * sizeof(device::GPUFaceData));
    faceDataBuffers[frameIndex].unmap();
    faceDataBuffers[frameIndex].flush(0,
                                       count * sizeof(device::GPUFaceData));
  }
}

void RenderComponent::updateUniforms(uint32_t frameIndex,
                                     const glm::mat4 &model,
                                     const glm::mat4 &view,
                                     const glm::mat4 &proj) {
  std::lock_guard lock(mutex_);
  if (!initialized || frameIndex >= uniformBuffers.size()) {
    return;
  }
  // This overload is for 3-D components.  Guard against dimension mismatch:
  // a 2-D component must use the mat3 overload so that the UBO is written
  // to the correctly-sized buffer that was allocated in initialize().
  if (dimension_ != 3) {
    return;
  }

  GPUUniformBufferData<3> gpu{};
  gpu.fromUBO(UniformBufferData<3>{model, view, proj});

  void *mapped = uniformBuffers[frameIndex].map();
  if (mapped != nullptr) {
    std::memcpy(mapped, &gpu, sizeof(gpu));
    uniformBuffers[frameIndex].unmap();
    uniformBuffers[frameIndex].flush(0, sizeof(gpu));
  }
}

void RenderComponent::updateUniforms(uint32_t frameIndex,
                                     const glm::mat3 &model,
                                     const glm::mat3 &view,
                                     const glm::mat3 &proj) {
  std::lock_guard lock(mutex_);
  if (!initialized || frameIndex >= uniformBuffers.size()) {
    return;
  }
  // This overload is for 2-D components.  Guard against dimension mismatch:
  // a 3-D component must use the mat4 overload so that the UBO is written
  // to the correctly-sized buffer that was allocated in initialize().
  if (dimension_ != 2) {
    return;
  }

  GPUUniformBufferData<2> gpu{};
  gpu.fromUBO(UniformBufferData<2>{model, view, proj});

  void *mapped = uniformBuffers[frameIndex].map();
  if (mapped != nullptr) {
    std::memcpy(mapped, &gpu, sizeof(gpu));
    uniformBuffers[frameIndex].unmap();
    uniformBuffers[frameIndex].flush(0, sizeof(gpu));
  }
}

void RenderComponent::draw(vk::CommandBuffer cmd,
                           uint32_t frameIndex) const {
  std::lock_guard lock(mutex_);
  if (!initialized) {
    return;
  }

  uploadFaceData(frameIndex, mesh_->getFaceCount());
  if (indirectCommandDirty) {
    uploadIndirectCommand();
  }

  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline->pipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         **pipeline->pipelineLayout, 0,
                         {*descriptorSets[frameIndex]}, {});
  if (bindlessDescriptorSet) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           **pipeline->pipelineLayout, 1,
                           {bindlessDescriptorSet}, {});
  }

  if (pipelineConfig.pushConstantSize >=
      sizeof(device::BindlessPushConstants)) {
    device::BindlessPushConstants pc(time, baseTextureId.index,
                                     mesh_->vertexCount(),
                                     static_cast<uint32_t>(mesh_->indices.size()));
    cmd.pushConstants(**pipeline->pipelineLayout,
                      pipelineConfig.pushConstantStages, 0, sizeof(pc), &pc);
  }

  cmd.bindIndexBuffer(mesh_->indexBuffer.getBuffer(), 0,
                      vk::IndexType::eUint32);
  cmd.drawIndexedIndirect(indirectDrawBuffer.getBuffer(), 0, 1,
                          sizeof(VkDrawIndexedIndirectCommand));
}

void RenderComponent::preRender(vk::CommandBuffer cmd,
                                uint32_t frameIndex) const {
  std::lock_guard lock(mutex_);
  if (!computeUpdatePipeline) {
    return;
  }

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute,
                   **computeUpdatePipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         **pipeline->pipelineLayout, 0,
                         {*descriptorSets[frameIndex]}, {});
  if (bindlessDescriptorSet) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **pipeline->pipelineLayout, 1,
                           {bindlessDescriptorSet}, {});
  }

  device::BindlessPushConstants pc(time, baseTextureId.index,
                                   mesh_->vertexCount(),
                                   static_cast<uint32_t>(mesh_->indices.size()));
  cmd.pushConstants(**pipeline->pipelineLayout,
                    pipelineConfig.pushConstantStages, 0, sizeof(pc), &pc);

  const uint32_t groups = (mesh_->vertexCount() + 63u) / 64u;
  cmd.dispatch(groups, 1, 1);

  vk::BufferMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.buffer = displacedPositionBuffers[frameIndex].getBuffer();
  barrier.size   = VK_WHOLE_SIZE;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eVertexShader, {}, {},
                      {barrier}, {});
}

bool RenderComponent::initializeCompute(
    device::GPUDevice &device, device::ShaderManager &shader,
    const device::ShaderTag *computeUpdateTag,
    const device::ShaderTag *computeNormalTag, uint32_t vertexCount,
    uint32_t indexCount) {
  if (!initialized || !pipeline) {
    return false;
  }
  vk::PipelineLayout sharedLayout = **pipeline->pipelineLayout;

  if (computeUpdateTag != nullptr) {
    auto res = shader.acquire(computeUpdateTag);
    if (res.has_value() && res.value() && res.value()->compute &&
        res.value()->compute->isValid) {
      vk::ComputePipelineCreateInfo ci{
          {}, res.value()->compute->getStageInfo(), sharedLayout};
      try {
        computeUpdatePipeline = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr, ci);
      } catch (...) {
      }
      shader.release(computeUpdateTag);
    }
  }

  if (computeNormalTag != nullptr && vertexCount > 0) {
    auto res = shader.acquire(computeNormalTag);
    if (res.has_value() && res.value() && res.value()->compute &&
        res.value()->compute->isValid) {
      std::unique_ptr<vk::raii::Pipeline> normalPipeline;
      try {
        normalPipeline = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr,
            vk::ComputePipelineCreateInfo{
                {}, res.value()->compute->getStageInfo(), sharedLayout});
      } catch (...) {
      }
      if (normalPipeline) {
        auto qf = device.getQueueFamilies();
        uint32_t qfIdx =
            qf.graphicsFamily.value_or(qf.computeFamily.value_or(0));
        vk::raii::CommandPool cmdPool(
            device.getRaiiDevice(),
            vk::CommandPoolCreateInfo{
                vk::CommandPoolCreateFlagBits::eTransient, qfIdx});
        auto cmdBufs = vk::raii::CommandBuffers(
            device.getRaiiDevice(),
            {*cmdPool, vk::CommandBufferLevel::ePrimary, 1});
        auto &cmd = cmdBufs[0];
        cmd.begin(vk::CommandBufferBeginInfo{
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **normalPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, sharedLayout,
                               0, {*descriptorSets[0]}, {});
        if (bindlessDescriptorSet) {
          cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                 sharedLayout, 1, {bindlessDescriptorSet},
                                 {});
        }
        device::BindlessPushConstants pc(0.0F, baseTextureId.index,
                                         vertexCount, indexCount);
        cmd.pushConstants(
            sharedLayout, pipelineConfig.pushConstantStages, 0,
            vk::ArrayProxy<const device::BindlessPushConstants>(pc));
        cmd.dispatch((vertexCount + 63u) / 64u, 1, 1);
        cmd.end();
        device.getGraphicsQueue().submit(
            vk::SubmitInfo{}.setCommandBuffers(*cmd));
        device.getGraphicsQueue().waitIdle();
      }
      shader.release(computeNormalTag);
    }
  }
  return true;
}

} // namespace ecs::component::object
