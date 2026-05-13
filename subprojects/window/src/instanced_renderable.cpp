#include "instanced_renderable.h"
#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include <algorithm>
#include <cstring>
#include <mutex>
#include <print>
#include <ranges>
#include <vk_mem_alloc_enums.hpp>

namespace window {

// ── BatchKeyHash ─────────────────────────────────────────────────────────────

std::size_t InstanceManager::BatchKeyHash::operator()(
    const BatchKey &k) const noexcept {
    std::size_t seed = 0;
    auto combine = [&](auto val) {
        std::hash<std::decay_t<decltype(val)>> h;
        seed ^= h(val) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    };
    combine(static_cast<VkBuffer>(k.vertexBuffer));
    combine(static_cast<VkBuffer>(k.indexBuffer));
    combine(static_cast<VkPipeline>(k.pipeline));
    return seed;
}

// ── findOrCreateBatch ────────────────────────────────────────────────────────

uint32_t InstanceManager::findOrCreateBatch(
    const BatchKey                     &key,
    const ecs::component::object::Mesh &mesh) {
    // Assumes mutex_ is already held by caller.
    auto it = batchIndex_.find(key);
    if (it != batchIndex_.end()) {
        return it->second;
    }

    if (batches_.size() >= kMaxBatches) {
        std::println(stderr,
                     "[InstanceManager] Batch limit ({}) reached", kMaxBatches);
        return UINT32_MAX;
    }

    uint32_t idx = static_cast<uint32_t>(batches_.size());
    BatchEntry entry;
    entry.key = key;
    entry.descriptor.firstIndex    = 0; // start of this mesh's index data
    entry.descriptor.indexCount    = static_cast<uint32_t>(mesh.indices.size());
    entry.descriptor.firstInstance = 0; // assigned when rebuilding per-batch offsets
    entry.descriptor.instanceCount = 0;
    entry.descriptor.boundsMin     = mesh.bounds.min;
    entry.descriptor.boundsMax     = mesh.bounds.max;
    entry.descriptorDirty          = true;

    batches_.push_back(std::move(entry));
    batchIndex_.emplace(key, idx);
    return idx;
}

// ── initialize ───────────────────────────────────────────────────────────────

bool InstanceManager::initialize(device::GPUDevice     &device,
                                 device::VMAAllocator  &allocator,
                                 device::ShaderManager &shaderManager,
                                 uint32_t               framesInFlight) {
    if (initialized_) {
        return false;
    }
    device_        = &device;
    allocator_     = &allocator;
    framesInFlight_ = framesInFlight;

    // ── CPU-side storage ────────────────────────────────────────────────────
    instances_.resize(kMaxInstances);
    active_.resize(kMaxInstances, false);
    dirty_.resize(kMaxInstances, false);
    freeList_.reserve(kMaxInstances);
    // Reverse order so pop_back() gives the lowest ID first.
    for (uint32_t i = kMaxInstances; i-- > 0;) {
        freeList_.push_back(i);
    }

    // ── GPU buffers ─────────────────────────────────────────────────────────
    frames_.resize(framesInFlight);
    for (uint32_t fi = 0; fi < framesInFlight; ++fi) {
        auto &fr = frames_[fi];

        fr.instanceBuffer = allocator.createHostVisibleStorageBuffer(
            kMaxInstances * sizeof(GPUInstanceData),
            "instance_buf_" + std::to_string(fi));
        if (!fr.instanceBuffer.isValid()) {
            std::println(stderr, "[InstanceManager] Failed to create instance buffer");
            return false;
        }

        fr.batchDescriptorBuf = allocator.createHostVisibleStorageBuffer(
            kMaxBatches * sizeof(GPUBatchDescriptor),
            "batch_desc_buf_" + std::to_string(fi));
        if (!fr.batchDescriptorBuf.isValid()) {
            std::println(stderr,
                         "[InstanceManager] Failed to create batch descriptor buffer");
            return false;
        }

        // Indirect draw buffer: needs eIndirectBuffer + eStorageBuffer usage.
        device::BufferCreateInfo indInfo{};
        indInfo.size = kMaxBatches * sizeof(VkDrawIndexedIndirectCommand);
        indInfo.usage =
            vk::BufferUsageFlagBits::eIndirectBuffer |
            vk::BufferUsageFlagBits::eStorageBuffer;
        indInfo.memoryUsage = vma::MemoryUsage::eCpuToGpu;
        indInfo.flags       = vma::AllocationCreateFlagBits::eMapped;
        indInfo.debugName   = "indirect_buf_" + std::to_string(fi);
        fr.indirectBuffer   = allocator.createBuffer(indInfo);
        if (!fr.indirectBuffer.isValid()) {
            std::println(stderr,
                         "[InstanceManager] Failed to create indirect buffer");
            return false;
        }
    }

    // ── Descriptor set layout (2 SSBOs: batch descriptors + indirect cmds) ──
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        {0, vk::DescriptorType::eStorageBuffer, 1,
         vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageBuffer, 1,
         vk::ShaderStageFlagBits::eCompute},
    };
    try {
        computeSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
            device.getRaiiDevice(),
            vk::DescriptorSetLayoutCreateInfo{{}, bindings});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[InstanceManager] Descriptor set layout creation failed: {}",
                     e.what());
        return false;
    }

    // ── Descriptor pool ─────────────────────────────────────────────────────
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eStorageBuffer, framesInFlight * 2}};
    try {
        computePool_ = std::make_unique<vk::raii::DescriptorPool>(
            device.getRaiiDevice(),
            vk::DescriptorPoolCreateInfo{
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                framesInFlight, poolSizes});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[InstanceManager] Descriptor pool creation failed: {}",
                     e.what());
        return false;
    }

    // ── Descriptor sets ─────────────────────────────────────────────────────
    try {
        std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                                      **computeSetLayout_);
        auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(),
                                              {**computePool_, layouts});

        const vk::DeviceSize batchBufSize =
            kMaxBatches * sizeof(GPUBatchDescriptor);
        const vk::DeviceSize indBufSize =
            kMaxBatches * sizeof(VkDrawIndexedIndirectCommand);

        computeSets_.reserve(framesInFlight);
        for (uint32_t fi = 0; fi < framesInFlight; ++fi) {
            computeSets_.push_back(std::move(sets[fi]));

            vk::DescriptorBufferInfo batchInfo{
                frames_[fi].batchDescriptorBuf.getBuffer(), 0, batchBufSize};
            vk::DescriptorBufferInfo indInfo{
                frames_[fi].indirectBuffer.getBuffer(), 0, indBufSize};

            std::vector<vk::WriteDescriptorSet> writes = {
                {*computeSets_[fi], 0, 0, 1,
                 vk::DescriptorType::eStorageBuffer, nullptr, &batchInfo},
                {*computeSets_[fi], 1, 0, 1,
                 vk::DescriptorType::eStorageBuffer, nullptr, &indInfo},
            };
            device.getRaiiDevice().updateDescriptorSets(writes, {});
        }
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[InstanceManager] Descriptor set allocation failed: {}",
                     e.what());
        return false;
    }

    // ── Compute pipeline layout ──────────────────────────────────────────────
    // Push constant: BuildParams (batchCount: uint + 3 pad = 16 bytes)
    vk::PushConstantRange pcRange{vk::ShaderStageFlagBits::eCompute, 0, 16};
    {
        const vk::DescriptorSetLayout dsLayout = **computeSetLayout_;
        try {
            computePipelineLayout_ = std::make_unique<vk::raii::PipelineLayout>(
                device.getRaiiDevice(),
                vk::PipelineLayoutCreateInfo{{}, 1, &dsLayout, 1, &pcRange});
        } catch (const std::exception &e) {
            std::println(stderr,
                         "[InstanceManager] Pipeline layout creation failed: {}",
                         e.what());
            return false;
        }
    }

    // ── Compute pipeline (indirect_build.slang) ──────────────────────────────
    static constexpr device::ShaderTag kIndirectBuildTag{
        "indirect_build",
        "shaders/indirect_build.slang",
        nullptr, nullptr, nullptr, "buildMain"};

    auto prog = shaderManager.acquire(&kIndirectBuildTag);
    if (!prog.has_value() || !*prog || !(*prog)->compute ||
        !(*prog)->compute->isValid) {
        std::println(stderr,
                     "[InstanceManager] Failed to compile indirect_build.slang");
        shaderManager.release(&kIndirectBuildTag);
        return false;
    }
    try {
        computePipeline_ = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr,
            vk::ComputePipelineCreateInfo{
                {}, (*prog)->compute->getStageInfo(), **computePipelineLayout_});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[InstanceManager] Compute pipeline creation failed: {}",
                     e.what());
        shaderManager.release(&kIndirectBuildTag);
        return false;
    }
    shaderManager.release(&kIndirectBuildTag);

    initialized_ = true;
    std::println("[InstanceManager] Initialised ({} frames in flight)",
                 framesInFlight);
    return true;
}

// ── shutdown ──────────────────────────────────────────────────────────────────

void InstanceManager::shutdown() {
    if (!initialized_) {
        return;
    }
    computePipeline_.reset();
    computePipelineLayout_.reset();
    computeSets_.clear();
    computePool_.reset();
    computeSetLayout_.reset();
    frames_.clear();
    initialized_ = false;
}

// ── registerInstance ─────────────────────────────────────────────────────────

bool InstanceManager::registerInstance(
    InstancedRenderable                    &ir,
    const ecs::component::object::Mesh     &mesh,
    vk::Pipeline                            pipeline) {
    if (!initialized_) {
        return false;
    }
    if (!mesh.gpuUploaded) {
        std::println(stderr,
                     "[InstanceManager] Mesh not GPU-uploaded");
        return false;
    }

    std::lock_guard lock(mutex_);

    if (freeList_.empty()) {
        std::println(stderr,
                     "[InstanceManager] Instance limit ({}) reached",
                     kMaxInstances);
        return false;
    }

    // Allocate an instance slot.
    uint32_t instanceId = freeList_.back();
    freeList_.pop_back();

    // Find or create the DrawBatch for this mesh + pipeline.
    BatchKey key{
        mesh.positionBuffer.getBuffer(),
        mesh.indexBuffer.getBuffer(),
        pipeline};
    uint32_t batchId = findOrCreateBatch(key, mesh);
    if (batchId == UINT32_MAX) {
        freeList_.push_back(instanceId); // return slot
        return false;
    }

    // Populate instance data.
    instances_[instanceId].transform  = ir.transform;
    instances_[instanceId].materialId = ir.materialId;
    instances_[instanceId].meshId     = batchId;
    active_[instanceId]               = true;
    dirty_[instanceId]                = true;

    // Register slot in batch.
    batches_[batchId].instanceSlots.push_back(instanceId);
    batches_[batchId].descriptor.instanceCount++;
    batches_[batchId].descriptorDirty = true;

    // Assign IDs back to the component.
    ir.instanceId = instanceId;
    ir.batchId    = batchId;
    ir.dirty      = false;

    return true;
}

// ── unregisterInstance ───────────────────────────────────────────────────────

void InstanceManager::unregisterInstance(uint32_t instanceId) {
    if (!initialized_ || instanceId >= kMaxInstances) {
        return;
    }

    std::lock_guard lock(mutex_);

    if (!active_[instanceId]) {
        return;
    }

    uint32_t batchId = instances_[instanceId].meshId;

    // Remove from batch slot list.
    if (batchId < batches_.size()) {
        auto &slots = batches_[batchId].instanceSlots;
        slots.erase(std::ranges::find(slots, instanceId));
        batches_[batchId].descriptor.instanceCount--;
        batches_[batchId].descriptorDirty = true;
    }

    active_[instanceId] = false;
    dirty_[instanceId]  = false;
    freeList_.push_back(instanceId);
}

// ── uploadInstances ───────────────────────────────────────────────────────────

void InstanceManager::uploadInstances(uint32_t frameIndex) {
    if (!initialized_ || frameIndex >= framesInFlight_) {
        return;
    }

    auto &fr = frames_[frameIndex];

    std::lock_guard lock(mutex_);

    // ── Instance buffer ─────────────────────────────────────────────────────
    {
        void *dst = fr.instanceBuffer.map();
        if (dst) {
            std::memcpy(dst, instances_.data(),
                        kMaxInstances * sizeof(GPUInstanceData));
            fr.instanceBuffer.unmap();
            fr.instanceBuffer.flush(0, kMaxInstances * sizeof(GPUInstanceData));
        }
    }

    // ── Batch descriptor buffer ─────────────────────────────────────────────
    // Recompute firstInstance offsets then upload.
    {
        uint32_t instanceOffset = 0;
        for (auto &batch : batches_) {
            batch.descriptor.firstInstance = instanceOffset;
            instanceOffset += batch.descriptor.instanceCount;
            batch.descriptorDirty = false;
        }

        void *dst = fr.batchDescriptorBuf.map();
        if (dst && !batches_.empty()) {
            std::vector<GPUBatchDescriptor> descs;
            descs.reserve(batches_.size());
            for (const auto &b : batches_) {
                descs.push_back(b.descriptor);
            }
            const vk::DeviceSize uploadSize =
                descs.size() * sizeof(GPUBatchDescriptor);
            std::memcpy(dst, descs.data(), uploadSize);
            fr.batchDescriptorBuf.unmap();
            fr.batchDescriptorBuf.flush(0, uploadSize);
        } else if (dst) {
            fr.batchDescriptorBuf.unmap();
        }
    }

    // Mark all slots as clean; the full-buffer memcpy covers every frame.
    std::fill(dirty_.begin(), dirty_.end(), false);
}

// ── buildIndirectCommands ────────────────────────────────────────────────────

void InstanceManager::buildIndirectCommands(vk::CommandBuffer cmd,
                                            uint32_t          frameIndex) const {
    if (!initialized_ || frameIndex >= framesInFlight_) {
        return;
    }

    uint32_t batchCount = 0;
    {
        std::lock_guard lock(mutex_);
        batchCount = static_cast<uint32_t>(batches_.size());
    }
    if (batchCount == 0) {
        return;
    }

    const uint32_t groupCount = (batchCount + 63u) / 64u;

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **computePipelineLayout_, 0,
                           {*computeSets_[frameIndex]}, {});

    struct BuildParams {
        uint32_t batchCount;
        uint32_t pad[3];
    };
    BuildParams pc{batchCount, {0, 0, 0}};
    cmd.pushConstants(**computePipelineLayout_,
                      vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(pc), &pc);

    cmd.dispatch(groupCount, 1, 1);
}

// ── draw ─────────────────────────────────────────────────────────────────────

void InstanceManager::draw(vk::CommandBuffer cmd,
                           uint32_t          frameIndex) const {
    if (!initialized_ || frameIndex >= framesInFlight_) {
        return;
    }

    std::lock_guard lock(mutex_);
    if (batches_.empty()) {
        return;
    }

    vk::Buffer indirectBuf = frames_[frameIndex].indirectBuffer.getBuffer();

    for (uint32_t bi = 0; bi < static_cast<uint32_t>(batches_.size()); ++bi) {
        const auto &batch = batches_[bi];
        if (batch.descriptor.instanceCount == 0) {
            continue;
        }

        // Bind the mesh geometry buffers.
        const vk::Buffer vertBuf = batch.key.vertexBuffer;
        const vk::Buffer idxBuf  = batch.key.indexBuffer;

        cmd.bindVertexBuffers(0, {vertBuf}, {vk::DeviceSize{0}});
        cmd.bindIndexBuffer(idxBuf, 0, vk::IndexType::eUint32);

        // Bind the batch's graphics pipeline.
        if (batch.key.pipeline) {
            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, batch.key.pipeline);
        }

        // Issue the indirect draw (one command at slot bi in the indirect buffer).
        const vk::DeviceSize cmdOffset =
            static_cast<vk::DeviceSize>(bi) *
            sizeof(VkDrawIndexedIndirectCommand);
        cmd.drawIndexedIndirect(indirectBuf, cmdOffset, 1,
                                sizeof(VkDrawIndexedIndirectCommand));
    }
}

// ── Accessors ─────────────────────────────────────────────────────────────────

vk::Buffer InstanceManager::getIndirectBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frames_.size()) {
        return {};
    }
    return frames_[frameIndex].indirectBuffer.getBuffer();
}

vk::Buffer InstanceManager::getInstanceBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frames_.size()) {
        return {};
    }
    return frames_[frameIndex].instanceBuffer.getBuffer();
}

vk::Buffer InstanceManager::getBatchDescriptorBuffer(uint32_t frameIndex) const {
    if (frameIndex >= frames_.size()) {
        return {};
    }
    return frames_[frameIndex].batchDescriptorBuf.getBuffer();
}

uint32_t InstanceManager::getBatchCount() const {
    std::lock_guard lock(mutex_);
    return static_cast<uint32_t>(batches_.size());
}

} // namespace window
