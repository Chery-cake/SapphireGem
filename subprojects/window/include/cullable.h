#ifndef CULLABLE_H_
#define CULLABLE_H_

#include "instanced_renderable.h"
#include "mesh.h"
#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace window {

/**
 * @brief Six-plane camera frustum.
 *
 * Each plane satisfies:  dot(plane.xyz, worldPos) + plane.w >= 0  ↔  inside.
 * The planes are ordered: left, right, bottom, top, near, far.
 */
struct WINDOW_API Frustum {
    glm::vec4 planes[6]; ///< xyz = normal, w = offset (unnormalised)

    /**
     * @brief Extract the six frustum planes from a combined view–projection
     *        matrix using the Gribb–Hartman method.
     *
     * @param viewProj  Combined view × projection matrix (column-major, GLM).
     * @return Frustum with the six clip-space planes.
     */
    [[nodiscard]] static Frustum fromViewProj(const glm::mat4 &viewProj);
};

/**
 * @brief Component marking an entity as subject to GPU frustum culling.
 *
 * Attach to entities that also have an @ref InstancedRenderable component.
 * The @ref FrustumCullManager dispatches a compute shader each frame that
 * tests the batch's world-space AABB against the camera frustum and zeroes
 * @c instanceCount in the indirect draw buffer for any culled batch.
 */
struct WINDOW_API Cullable {
    ecs::component::object::BoundingBox localBounds; ///< AABB in local (mesh) space
    uint32_t batchId = UINT32_MAX;                   ///< Batch index from InstancedRenderable
};

/**
 * @brief Push-constant layout for @c frustum_cull.slang.
 *
 * Must match the @c CullParams struct in the shader exactly.
 * Total size: 112 bytes (16-byte header + 6 × 16-byte planes).
 */
struct WINDOW_API GPUCullParams {
    uint32_t  batchCount = 0;
    uint32_t  pad[3]{};
    glm::vec4 frustumPlanes[6]{};
};
static_assert(sizeof(GPUCullParams) == 112,
              "GPUCullParams must be 112 bytes");

/**
 * @brief Dispatches the GPU frustum-culling compute pass for an InstanceManager.
 *
 * Each frame, @ref cull:
 *  1. Extracts six frustum planes from @p viewProj (Gribb–Hartman).
 *  2. Fills the @c CullParams push constants.
 *  3. Dispatches @c frustum_cull.slang — one thread per DrawBatch.
 *     Invisible batches have their @c instanceCount zeroed in the indirect
 *     command buffer owned by the associated @ref InstanceManager.
 *
 * Register FrustumCullManager with @ref AsyncComputeManager so @ref cull is
 * called from the dedicated compute command buffer:
 * @code
 *   asyncComputeManager.registerEffect(&myRenderComponent,
 *       ComputePriority::High,
 *       [&cm, &frameData, frameIdx](vk::CommandBuffer cmd, uint32_t fi) {
 *           cm.cull(cmd, fi, frameData.view * frameData.proj);
 *       });
 * @endcode
 */
class WINDOW_API FrustumCullManager {
public:
    FrustumCullManager() = default;
    ~FrustumCullManager() { shutdown(); }

    // Non-copyable, non-moveable
    FrustumCullManager(const FrustumCullManager &) = delete;
    FrustumCullManager &operator=(const FrustumCullManager &) = delete;
    FrustumCullManager(FrustumCullManager &&) = delete;
    FrustumCullManager &operator=(FrustumCullManager &&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Create the frustum-cull compute pipeline and descriptor sets.
     *
     * @param device          GPU device.
     * @param shaderManager   Shader manager (compiles @c frustum_cull.slang).
     * @param instanceManager The InstanceManager whose indirect buffer is culled.
     *                        Must already be initialised.
     * @param framesInFlight  Frames in flight.
     * @return true on success.
     */
    bool initialize(device::GPUDevice     &device,
                    device::ShaderManager &shaderManager,
                    InstanceManager       &instanceManager,
                    uint32_t               framesInFlight);

    /// Release all GPU resources.
    void shutdown();

    // ── Per-frame ──────────────────────────────────────────────────────────

    /**
     * @brief Dispatch the frustum-cull compute shader.
     *
     * Zeros @c instanceCount in the indirect command buffer for any DrawBatch
     * whose world-space AABB (using the first instance's transform) falls
     * entirely outside the camera frustum.
     *
     * Must be called after @ref InstanceManager::buildIndirectCommands has
     * populated the indirect command buffer for this frame.
     *
     * @param cmd        Active compute command buffer.
     * @param frameIndex Current frame-in-flight index.
     * @param viewProj   Combined view × projection matrix.
     */
    void cull(vk::CommandBuffer cmd,
              uint32_t          frameIndex,
              const glm::mat4  &viewProj) const;

    [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
    std::unique_ptr<vk::raii::DescriptorSetLayout> computeSetLayout_;
    std::unique_ptr<vk::raii::DescriptorPool>      computePool_;
    std::unique_ptr<vk::raii::PipelineLayout>      computePipelineLayout_;
    std::unique_ptr<vk::raii::Pipeline>            computePipeline_;

    // Per-frame descriptor sets (bindings point to InstanceManager buffers).
    std::vector<vk::raii::DescriptorSet> frameSets_;

    InstanceManager   *instanceManager_ = nullptr;
    device::GPUDevice *device_          = nullptr;
    uint32_t           framesInFlight_  = 0;
    bool               initialized_     = false;
};

} // namespace window

#endif // CULLABLE_H_
