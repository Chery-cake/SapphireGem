#pragma once
#include "bindless_types.h"
#include "glm/ext/matrix_transform.hpp"
#include "material.h"
#include "object.h"
#include "pipeline_cache.h"
#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <cstdint>
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
// Mesh
// ============================================================================

template <uint32_t Dim> void Mesh<Dim>::calculateFaces() {
  faces.clear();

  if (indices.empty()) {
    // No indices: treat each set of 3 vertices as one face
    uint32_t totalVerts = static_cast<uint32_t>(vertices.size());
    uint32_t faceCount = totalVerts / 3;
    faces.reserve(faceCount);

    std::ranges::for_each(
        std::views::iota(0U, faceCount),
        [&faces = faces](uint32_t i) { faces.push_back({i, i * 3, 3}); });
  } else {
    // Every 3 consecutive indices form one triangular face
    uint32_t faceCount = static_cast<uint32_t>(indices.size()) / 3;
    faces.reserve(faceCount);
    std::ranges::for_each(
        std::views::iota(0U, faceCount),
        [&faces = faces](uint32_t i) { faces.push_back({i, i * 3, 3}); });
  }
}

template <uint32_t Dim>
bool Mesh<Dim>::upload(device::VMAAllocator &allocator) {
  if (vertices.empty()) {
    return false;
  }

  const size_t vertCount = std::max(vertices.size(), size_t(1));
  const size_t idxCount = std::max(indices.size(), size_t(1));

  // Base positions
  positionBuffer = allocator.createHostVisibleStorageBuffer(
      vertCount * sizeof(device::GPUVertexPosition), name + "_basePos");
  if (!positionBuffer.isValid()) {
    return false;
  }

  std::vector<device::GPUVertexPosition> gpuPos(vertCount);
  std::ranges::for_each(std::views::iota(0U, vertCount),
                        [&vertices = vertices, &gpuPos](size_t i) {
                          auto &v = vertices[i];
                          device::GPUVertexPosition gp{};
                          if constexpr (Dim >= 1) {
                            gp.x = v.position[0];
                          }
                          if constexpr (Dim >= 2) {
                            gp.y = v.position[1];
                          }
                          if constexpr (Dim >= 3) {
                            gp.z = v.position[2];
                          }
                          gp.w = 1.0F;

                          gp.r = v.color[0];
                          gp.g = v.color[1];
                          gp.b = v.color[2];

                          gp.nx = 0;
                          gp.ny = 1;
                          gp.nz = 0; // default normal (overwritten later)

                          gpuPos[i] = gp;
                        });

  void *mapped = positionBuffer.map();
  if (mapped == nullptr) {
    return false;
  }
  std::memcpy(mapped, gpuPos.data(),
              vertCount * sizeof(device::GPUVertexPosition));
  positionBuffer.unmap();
  positionBuffer.flush(0, vertCount * sizeof(device::GPUVertexPosition));

  // Index buffer (dual‑use as SSBO)
  indexBuffer = allocator.createIndexStorageBuffer(idxCount * sizeof(uint32_t),
                                                   name + "_indices");
  if (!indexBuffer.isValid()) {
    return false;
  }
  mapped = indexBuffer.map();
  if (mapped != nullptr) {
    std::memcpy(mapped, indices.data(), indices.size() * sizeof(uint32_t));
    indexBuffer.unmap();
    indexBuffer.flush(0, indices.size() * sizeof(uint32_t));
  }

  gpuUploaded = true;
  return true;
}

// ============================================================================
// Render Component
// ============================================================================

template <uint32_t Dim>
bool RenderComponent<Dim>::initialize(device::VMAAllocator &allocator,
                                      device::GPUDevice &device,
                                      const Mesh<Dim> &mesh,
                                      const window::Material &baseMaterial,
                                      vk::RenderPass renderPass,
                                      uint32_t framesInFlight,
                                      const window::PipelineConfig &config,
                                      vk::DescriptorSetLayout bindlessLayout) {
  std::lock_guard lock(mutex);

  if (initialized) {
    return false;
  }
  if (!mesh.gpuUploaded) {
    return false;
  }
  this->mesh = &mesh;

  pipelineConfig = config;
  const size_t vertCount = std::max(mesh.vertices.size(), size_t(1));
  const size_t faceCount = std::max(mesh.faces.size(), size_t(1));
  const size_t idxCount = std::max(mesh.indices.size(), size_t(1));

  // --- Descriptor set layout ---
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      {0, vk::DescriptorType::eUniformBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute},
      {1, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eFragment | vk::ShaderStageFlagBits::eCompute},
      {2, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment |
           vk::ShaderStageFlagBits::eCompute},
      {3, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute},
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

  // --- Pipeline ---
  pipeline = window::PipelineCache::instance().getOrCreate(
      device, renderPass, **descriptorSetLayout, config, 0, bindlessLayout,
      baseMaterial.getShaderProgram());
  if (!pipeline) {
    return false;
  }

  // --- UBOs ---
  const vk::DeviceSize uboSize = sizeof(GPUUniformBufferData<Dim>);
  uniformBuffers.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto buf =
        allocator.createUniformBuffer(uboSize, "ubo_" + std::to_string(i));
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
  const vk::DeviceSize faceDataSize = faceCount * sizeof(device::GPUFaceData);
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
      vertCount * sizeof(device::GPUVertexPosition);
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
  // Initialise displaced positions with base data
  void *src = mesh.positionBuffer.map();
  if (src != nullptr) {
    std::ranges::for_each(std::views::iota(0U, framesInFlight),
                          [&displacedPositionBuffers = displacedPositionBuffers,
                           &posDataSize, src](uint32_t i) {
                            void *dst = displacedPositionBuffers[i].map();
                            if (dst) {
                              std::memcpy(dst, src, posDataSize);
                              displacedPositionBuffers[i].unmap();
                              displacedPositionBuffers[i].flush(0, posDataSize);
                            }
                          });
    mesh.positionBuffer.unmap();
  }

  // --- Descriptor pool & sets ---
  std::vector<vk::DescriptorPoolSize> poolSizes = {
      {vk::DescriptorType::eUniformBuffer, framesInFlight},
      {vk::DescriptorType::eStorageBuffer, framesInFlight * 4}};
  descriptorPool = std::make_unique<vk::raii::DescriptorPool>(
      device.getRaiiDevice(),
      vk::DescriptorPoolCreateInfo{
          vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, framesInFlight,
          poolSizes});

  std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                               **descriptorSetLayout);
  auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(),
                                       {**descriptorPool, layouts});
  descriptorSets.reserve(sets.size());
  std::ranges::for_each(sets, [&descriptorSets = descriptorSets](auto &s) {
    descriptorSets.push_back(std::move(s));
  });

  std::ranges::for_each(std::views::iota(0U, framesInFlight), [&](uint32_t i) {
    std::vector<vk::WriteDescriptorSet> writes;

    vk::DescriptorBufferInfo uniformBuffer{uniformBuffers[i].getBuffer(), 0,
                                           uboSize};
    vk::DescriptorBufferInfo faceDataBuffer{faceDataBuffers[i].getBuffer(), 0,
                                            faceDataSize};
    vk::DescriptorBufferInfo positionBuffer{mesh.positionBuffer.getBuffer(), 0,
                                            posDataSize};
    vk::DescriptorBufferInfo displacedPositionBuffer{
        displacedPositionBuffers[i].getBuffer(), 0, posDataSize};
    vk::DescriptorBufferInfo indexBuffer{mesh.indexBuffer.getBuffer(), 0,
                                         idxCount * sizeof(uint32_t)};

    writes.emplace_back(*descriptorSets[i], 0, 0, 1,
                        vk::DescriptorType::eUniformBuffer, nullptr,
                        &uniformBuffer, nullptr);
    writes.emplace_back(*descriptorSets[i], 1, 0, 1,
                        vk::DescriptorType::eStorageBuffer, nullptr,
                        &faceDataBuffer, nullptr);
    writes.emplace_back(*descriptorSets[i], 2, 0, 1,
                        vk::DescriptorType::eStorageBuffer, nullptr,
                        &positionBuffer, nullptr);
    writes.emplace_back(*descriptorSets[i], 3, 0, 1,
                        vk::DescriptorType::eStorageBuffer, nullptr,
                        &displacedPositionBuffer, nullptr);
    writes.emplace_back(*descriptorSets[i], 4, 0, 1,
                        vk::DescriptorType::eStorageBuffer, nullptr,
                        &indexBuffer, nullptr);

    device.getRaiiDevice().updateDescriptorSets(writes, {});
  });

  // Upload initial face data
  std::ranges::for_each(std::views::iota(0U, framesInFlight),
                        [&](uint32_t i) { uploadFaceData(i, faceCount); });

  initialized = true;
  return true;
}

template <uint32_t Dim> void RenderComponent<Dim>::release() {
  std::lock_guard lock(mutex);
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
  indirectCommandDirty = true;
  initialized = false;
}

template <uint32_t Dim>
void RenderComponent<Dim>::uploadIndirectCommand() const {
  std::lock_guard lock(mutex);
  // For simplicity, use a shared staging buffer or map if host-visible.
  // Here we assume the indirect buffer is host-visible (CpuToGpu) for easy
  // updates. If it's device-local, you'd use a one-time staging copy.
  // TODO improve implementation
  void *mapped = indirectDrawBuffer.map();
  if (mapped != nullptr) {
    std::memcpy(mapped, &indirectDrawCmd,
                sizeof(vk::DrawIndexedIndirectCommand));
    indirectDrawBuffer.unmap();
    indirectDrawBuffer.flush(0, sizeof(vk::DrawIndexedIndirectCommand));
  }
  indirectCommandDirty = false;
}

template <uint32_t Dim>
void RenderComponent<Dim>::setFaceMaterial(uint32_t faceIndex,
                                           const device::FaceMaterial &desc,
                                           size_t faceCount) {
  std::lock_guard lock(mutex);
  if (faceIndex >= faceCount) {
    return;
  }
  if (faceMaterials.size() <= faceIndex) {
    faceMaterials.resize(faceIndex + 1);
  }
  faceMaterials[faceIndex] = desc;
}

template <uint32_t Dim>
device::FaceMaterial
RenderComponent<Dim>::getFaceMaterial(uint32_t faceIndex) const {
  std::lock_guard lock(mutex);
  if (faceIndex < faceMaterials.size()) {
    return faceMaterials[faceIndex];
  }
  return {};
}

template <uint32_t Dim>
void RenderComponent<Dim>::uploadFaceData(uint32_t frameIndex,
                                          size_t faceCount) const {
  std::lock_guard lock(mutex);
  if (!initialized || faceDataBuffers.empty() ||
      frameIndex >= faceDataBuffers.size()) {
    return;
  }

  const size_t count = std::max(faceCount, size_t(1));
  std::vector<device::GPUFaceData> gpuData(count);
  for (size_t i = 0; i < count; ++i) {
    if (i < faceMaterials.size()) {
      gpuData[i] = device::GPUFaceData::fromFaceMaterial(faceMaterials[i]);
    } else {
      gpuData[i] = device::GPUFaceData::fromFaceMaterial({});
    }
  }

  void *mapped = faceDataBuffers[frameIndex].map();
  if (mapped != nullptr) {
    std::memcpy(mapped, gpuData.data(), count * sizeof(device::GPUFaceData));
    faceDataBuffers[frameIndex].unmap();
    faceDataBuffers[frameIndex].flush(0, count * sizeof(device::GPUFaceData));
  }
}

template <uint32_t Dim>
void RenderComponent<Dim>::updateUniforms(

    uint32_t frameIndex, const TransformComponent<Dim> &transform,
    const typename TransformComponent<Dim>::MatType &view,
    const typename TransformComponent<Dim>::MatType &proj) {
  std::lock_guard lock(mutex);
  if (!initialized || frameIndex >= uniformBuffers.size()) {
    return;
  }

  UniformBufferData<Dim> ubo{transform.modelMatrix(), view, proj};
  GPUUniformBufferData<Dim> gpu;
  gpu.fromUBO(ubo);

  void *mapped = uniformBuffers[frameIndex].map();
  if (mapped != nullptr) {
    std::memcpy(mapped, &gpu, sizeof(gpu));
    uniformBuffers[frameIndex].unmap();
    uniformBuffers[frameIndex].flush(0, sizeof(gpu));
  }
}

template <uint32_t Dim>
void RenderComponent<Dim>::draw(vk::CommandBuffer cmd,
                                uint32_t frameIndex) const {
  std::lock_guard lock(mutex);
  if (!initialized) {
    return;
  }

  uploadFaceData(frameIndex, mesh->faces.size());
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
    device::BindlessPushConstants pc(
        time, baseTextureId.index, static_cast<uint32_t>(mesh->vertices.size()),
        static_cast<uint32_t>(mesh->indices.size()));
    cmd.pushConstants(**pipeline->pipelineLayout,
                      pipelineConfig.pushConstantStages, 0, sizeof(pc), &pc);
  }

  cmd.bindIndexBuffer(mesh->indexBuffer.getBuffer(), 0, vk::IndexType::eUint32);
  cmd.drawIndexedIndirect(indirectDrawBuffer.getBuffer(), 0, 1,
                          sizeof(VkDrawIndexedIndirectCommand));
}

template <uint32_t Dim>
void RenderComponent<Dim>::preRender(vk::CommandBuffer cmd,
                                     uint32_t frameIndex) const {
  std::lock_guard lock(mutex);
  if (!computeUpdatePipeline) {
    return;
  }
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computeUpdatePipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         **pipeline->pipelineLayout, 0,
                         {*descriptorSets[frameIndex]}, {});
  if (bindlessDescriptorSet) {
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **pipeline->pipelineLayout, 1,
                           {bindlessDescriptorSet}, {});
  }

  device::BindlessPushConstants pc(time, baseTextureId.index,
                                   static_cast<uint32_t>(mesh->vertices.size()),
                                   static_cast<uint32_t>(mesh->indices.size()));
  cmd.pushConstants(**pipeline->pipelineLayout,
                    pipelineConfig.pushConstantStages, 0, sizeof(pc), &pc);

  uint32_t groups = (static_cast<uint32_t>(mesh->vertices.size()) + 63) / 64;
  cmd.dispatch(groups, 1, 1);

  vk::BufferMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.buffer = displacedPositionBuffers[frameIndex].getBuffer();
  barrier.size = VK_WHOLE_SIZE;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eVertexShader, {}, {},
                      {barrier}, {});
}

template <uint32_t Dim>
bool RenderComponent<Dim>::initializeCompute(
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
        // one-shot dispatch
        auto qf = device.getQueueFamilies();
        uint32_t qfIdx =
            qf.graphicsFamily.value_or(qf.computeFamily.value_or(0));
        vk::raii::CommandPool cmdPool(
            device.getRaiiDevice(),
            vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eTransient,
                                      qfIdx});
        auto cmdBufs = vk::raii::CommandBuffers(
            device.getRaiiDevice(),
            {*cmdPool, vk::CommandBufferLevel::ePrimary, 1});
        auto &cmd = cmdBufs[0];
        cmd.begin(vk::CommandBufferBeginInfo{
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **normalPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, sharedLayout, 0,
                               {*descriptorSets[0]}, {});
        if (bindlessDescriptorSet) {
          cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, sharedLayout,
                                 1, {bindlessDescriptorSet}, {});
        }
        device::BindlessPushConstants pc(0.0F, baseTextureId.index, vertexCount,
                                         indexCount);
        cmd.pushConstants(
            sharedLayout, pipelineConfig.pushConstantStages, 0,
            vk::ArrayProxy<const device::BindlessPushConstants>(pc));
        cmd.dispatch((vertexCount + 63) / 64, 1, 1);
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
