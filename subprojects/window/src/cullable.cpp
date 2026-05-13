#include "cullable.h"
#include "shader_manager.h"
#include "vulkan_device.h"
#include <cstring>
#include <print>

namespace window {

// ── Frustum ───────────────────────────────────────────────────────────────────

Frustum Frustum::fromViewProj(const glm::mat4 &vp) {
    // Gribb–Hartman method: extract planes from the rows of the VP matrix.
    // GLM uses column-major storage: vp[col][row].
    // Row i is the vector { vp[0][i], vp[1][i], vp[2][i], vp[3][i] }.

    const glm::vec4 row0{vp[0][0], vp[1][0], vp[2][0], vp[3][0]};
    const glm::vec4 row1{vp[0][1], vp[1][1], vp[2][1], vp[3][1]};
    const glm::vec4 row2{vp[0][2], vp[1][2], vp[2][2], vp[3][2]};
    const glm::vec4 row3{vp[0][3], vp[1][3], vp[2][3], vp[3][3]};

    Frustum f{};
    // Planes ordered: left, right, bottom, top, near, far.
    // Vulkan clip-space depth: [0, 1], so near = row2, far = row3 − row2.
    f.planes[0] = row3 + row0; // left
    f.planes[1] = row3 - row0; // right
    f.planes[2] = row3 + row1; // bottom
    f.planes[3] = row3 - row1; // top
    f.planes[4] = row2;        // near
    f.planes[5] = row3 - row2; // far
    return f;
}

// ── FrustumCullManager::initialize ───────────────────────────────────────────

bool FrustumCullManager::initialize(device::GPUDevice     &device,
                                    device::ShaderManager &shaderManager,
                                    InstanceManager       &instanceManager,
                                    uint32_t               framesInFlight) {
    if (initialized_) {
        return false;
    }
    if (!instanceManager.isInitialized()) {
        std::println(stderr,
                     "[FrustumCullManager] InstanceManager must be initialised first");
        return false;
    }

    device_          = &device;
    instanceManager_ = &instanceManager;
    framesInFlight_  = framesInFlight;

    // ── Descriptor set layout (3 SSBOs) ─────────────────────────────────────
    // binding 0: batch descriptors  (read-only)
    // binding 1: instance data      (read-only)
    // binding 2: indirect commands  (read-write)
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        {0, vk::DescriptorType::eStorageBuffer, 1,
         vk::ShaderStageFlagBits::eCompute},
        {1, vk::DescriptorType::eStorageBuffer, 1,
         vk::ShaderStageFlagBits::eCompute},
        {2, vk::DescriptorType::eStorageBuffer, 1,
         vk::ShaderStageFlagBits::eCompute},
    };
    try {
        computeSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
            device.getRaiiDevice(),
            vk::DescriptorSetLayoutCreateInfo{{}, bindings});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[FrustumCullManager] Descriptor set layout failed: {}",
                     e.what());
        return false;
    }

    // ── Descriptor pool ──────────────────────────────────────────────────────
    std::vector<vk::DescriptorPoolSize> poolSizes = {
        {vk::DescriptorType::eStorageBuffer, framesInFlight * 3}};
    try {
        computePool_ = std::make_unique<vk::raii::DescriptorPool>(
            device.getRaiiDevice(),
            vk::DescriptorPoolCreateInfo{
                vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                framesInFlight, poolSizes});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[FrustumCullManager] Descriptor pool failed: {}",
                     e.what());
        return false;
    }

    // ── Descriptor sets ──────────────────────────────────────────────────────
    try {
        std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                                      **computeSetLayout_);
        auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(),
                                              {**computePool_, layouts});

        frameSets_.reserve(framesInFlight);
        for (uint32_t fi = 0; fi < framesInFlight; ++fi) {
            frameSets_.push_back(std::move(sets[fi]));

            vk::DescriptorBufferInfo batchInfo{
                instanceManager.getBatchDescriptorBuffer(fi),
                0,
                kMaxBatches * sizeof(GPUBatchDescriptor)};
            vk::DescriptorBufferInfo instInfo{
                instanceManager.getInstanceBuffer(fi),
                0,
                kMaxInstances * sizeof(GPUInstanceData)};
            vk::DescriptorBufferInfo indInfo{
                instanceManager.getIndirectBuffer(fi),
                0,
                kMaxBatches * sizeof(VkDrawIndexedIndirectCommand)};

            std::vector<vk::WriteDescriptorSet> writes = {
                {*frameSets_[fi], 0, 0, 1, vk::DescriptorType::eStorageBuffer,
                 nullptr, &batchInfo},
                {*frameSets_[fi], 1, 0, 1, vk::DescriptorType::eStorageBuffer,
                 nullptr, &instInfo},
                {*frameSets_[fi], 2, 0, 1, vk::DescriptorType::eStorageBuffer,
                 nullptr, &indInfo},
            };
            device.getRaiiDevice().updateDescriptorSets(writes, {});
        }
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[FrustumCullManager] Descriptor set allocation failed: {}",
                     e.what());
        return false;
    }

    // ── Pipeline layout (push constants = CullParams, 112 bytes) ────────────
    vk::PushConstantRange pcRange{vk::ShaderStageFlagBits::eCompute, 0,
                                   sizeof(GPUCullParams)};
    {
        const vk::DescriptorSetLayout dsLayout = **computeSetLayout_;
        try {
            computePipelineLayout_ = std::make_unique<vk::raii::PipelineLayout>(
                device.getRaiiDevice(),
                vk::PipelineLayoutCreateInfo{{}, 1, &dsLayout, 1, &pcRange});
        } catch (const std::exception &e) {
            std::println(stderr,
                         "[FrustumCullManager] Pipeline layout failed: {}",
                         e.what());
            return false;
        }
    }

    // ── Compute pipeline (frustum_cull.slang) ────────────────────────────────
    static constexpr device::ShaderTag kFrustumCullTag{
        "frustum_cull",
        "shaders/frustum_cull.slang",
        nullptr, nullptr, nullptr, "cullMain"};

    auto prog = shaderManager.acquire(&kFrustumCullTag);
    if (!prog.has_value() || !*prog || !(*prog)->compute ||
        !(*prog)->compute->isValid) {
        std::println(stderr,
                     "[FrustumCullManager] Failed to compile frustum_cull.slang");
        shaderManager.release(&kFrustumCullTag);
        return false;
    }
    try {
        computePipeline_ = std::make_unique<vk::raii::Pipeline>(
            device.getRaiiDevice(), nullptr,
            vk::ComputePipelineCreateInfo{
                {}, (*prog)->compute->getStageInfo(), **computePipelineLayout_});
    } catch (const std::exception &e) {
        std::println(stderr,
                     "[FrustumCullManager] Compute pipeline creation failed: {}",
                     e.what());
        shaderManager.release(&kFrustumCullTag);
        return false;
    }
    shaderManager.release(&kFrustumCullTag);

    initialized_ = true;
    std::println("[FrustumCullManager] Initialised ({} frames in flight)",
                 framesInFlight);
    return true;
}

// ── shutdown ──────────────────────────────────────────────────────────────────

void FrustumCullManager::shutdown() {
    if (!initialized_) {
        return;
    }
    computePipeline_.reset();
    computePipelineLayout_.reset();
    frameSets_.clear();
    computePool_.reset();
    computeSetLayout_.reset();
    initialized_ = false;
}

// ── cull ─────────────────────────────────────────────────────────────────────

void FrustumCullManager::cull(vk::CommandBuffer cmd,
                               uint32_t          frameIndex,
                               const glm::mat4  &viewProj) const {
    if (!initialized_ || frameIndex >= framesInFlight_) {
        return;
    }

    const uint32_t batchCount = instanceManager_->getBatchCount();
    if (batchCount == 0) {
        return;
    }

    // Build frustum planes and push constants.
    const Frustum frustum = Frustum::fromViewProj(viewProj);
    GPUCullParams pc{};
    pc.batchCount = batchCount;
    std::memcpy(pc.frustumPlanes, frustum.planes, sizeof(frustum.planes));

    const uint32_t groupCount = (batchCount + 63u) / 64u;

    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, **computePipeline_);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           **computePipelineLayout_, 0,
                           {*frameSets_[frameIndex]}, {});
    cmd.pushConstants(**computePipelineLayout_,
                      vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(GPUCullParams), &pc);
    cmd.dispatch(groupCount, 1, 1);
}

} // namespace window
