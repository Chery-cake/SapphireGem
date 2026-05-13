#ifndef INSTANCED_RENDERABLE_H_
#define INSTANCED_RENDERABLE_H_

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
#include <string>
#include <unordered_map>
#include <vector>

namespace window {

/// Maximum number of instances the InstanceManager can hold concurrently.
inline constexpr uint32_t kMaxInstances = 65536;

/// Maximum number of DrawBatches (unique mesh+pipeline combinations).
inline constexpr uint32_t kMaxBatches = 1024;

/**
 * @brief Per-instance GPU data uploaded to the instance SSBO.
 *
 * Layout matches the @c GPUInstanceData struct in @c indirect_build.slang and
 * @c frustum_cull.slang.  Total size: 80 bytes (std430-compatible).
 */
struct WINDOW_API GPUInstanceData {
    glm::mat4 transform{1.0f}; ///< World-space model matrix (64 bytes)
    uint32_t  materialId = 0;  ///< Index into the material / texture table
    uint32_t  meshId     = 0;  ///< Logical mesh ID (DrawBatch index)
    uint32_t  pad[2]{};        ///< Padding to 80 bytes
};
static_assert(sizeof(GPUInstanceData) == 80,
              "GPUInstanceData must be 80 bytes (std430)");

/**
 * @brief Per-batch GPU descriptor uploaded to the batch-descriptor SSBO.
 *
 * Consumed by @c indirect_build.slang to build @c VkDrawIndexedIndirectCommand
 * entries, and by @c frustum_cull.slang for per-batch AABB testing.
 * Total size: 48 bytes (std430-compatible).
 */
struct WINDOW_API GPUBatchDescriptor {
    uint32_t  firstIndex    = 0; ///< First index in the mesh index buffer
    uint32_t  indexCount    = 0; ///< Number of indices per draw (triangles × 3)
    uint32_t  firstInstance = 0; ///< First slot in the instance SSBO
    uint32_t  instanceCount = 0; ///< Total registered instances in this batch
    glm::vec3 boundsMin{0.0f};   ///< Local-space AABB minimum corner
    float     pad0 = 0.0f;
    glm::vec3 boundsMax{0.0f};   ///< Local-space AABB maximum corner
    float     pad1 = 0.0f;
};
static_assert(sizeof(GPUBatchDescriptor) == 48,
              "GPUBatchDescriptor must be 48 bytes (std430)");

/**
 * @brief Component for entities that participate in multi-draw-indirect rendering.
 *
 * Attach this component to an entity alongside a
 * @ref ecs::component::object::Mesh to donate the entity's transform to the
 * global @ref InstanceManager.  The manager groups entities that share the
 * same mesh and graphics pipeline into one @ref DrawBatch and issues a single
 * @c vkCmdDrawIndexedIndirect call for the whole group.
 *
 * Workflow:
 * @code
 *   // At scene load:
 *   InstancedRenderable ir;
 *   ir.transform  = entity.get<TransformComponent>().modelMatrix();
 *   ir.materialId = myMaterialIndex;
 *   manager.registerInstance(ir, mesh, graphicsPipeline);
 *
 *   // Per frame (before draw):
 *   ir.transform = updatedTransform;
 *   ir.dirty     = true;
 *   manager.uploadInstances(frameIndex);
 *   manager.buildIndirectCommands(computeCmd, frameIndex);
 *   manager.draw(graphicsCmd, frameIndex);
 * @endcode
 */
struct WINDOW_API InstancedRenderable {
    glm::mat4 transform{1.0f};         ///< World-space model matrix; update each frame
    uint32_t  materialId  = 0;         ///< Material / texture index used by the shader
    uint32_t  instanceId  = UINT32_MAX; ///< Slot index assigned by InstanceManager (read-only)
    uint32_t  batchId     = UINT32_MAX; ///< DrawBatch index assigned by InstanceManager (read-only)
    bool      dirty       = true;       ///< Set true whenever the transform changes
};

/**
 * @brief Global multi-draw-indirect instanced rendering manager.
 *
 * Collects per-instance transforms from @ref InstancedRenderable components,
 * uploads them to a GPU storage buffer, then dispatches:
 *  1. @c indirect_build.slang — writes one @c VkDrawIndexedIndirectCommand
 *     per @ref DrawBatch into the indirect draw buffer.
 *  2. Optionally @c frustum_cull.slang (via @ref FrustumCullManager) — zeros
 *     @c instanceCount for batches outside the camera frustum.
 *
 * Finally, @ref draw binds per-batch vertex/index buffers and issues
 * @c vkCmdDrawIndexedIndirect once per @ref DrawBatch.
 *
 * Thread-safety: @ref registerInstance and @ref unregisterInstance are
 * mutex-protected.  @ref uploadInstances, @ref buildIndirectCommands, and
 * @ref draw must be called from the render thread.
 */
class WINDOW_API InstanceManager {
public:
    InstanceManager() = default;
    ~InstanceManager() { shutdown(); }

    // Non-copyable, non-moveable (contains RAII Vulkan handles)
    InstanceManager(const InstanceManager &) = delete;
    InstanceManager &operator=(const InstanceManager &) = delete;
    InstanceManager(InstanceManager &&) = delete;
    InstanceManager &operator=(InstanceManager &&) = delete;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    /**
     * @brief Allocate GPU buffers and create the indirect-build compute pipeline.
     *
     * @param device          The GPU device.
     * @param allocator       VMA allocator for buffer creation.
     * @param shaderManager   Shader manager (compiles @c indirect_build.slang).
     * @param framesInFlight  Number of frames in flight (one buffer set each).
     * @return true on success.
     */
    bool initialize(device::GPUDevice    &device,
                    device::VMAAllocator &allocator,
                    device::ShaderManager &shaderManager,
                    uint32_t              framesInFlight);

    /// Release all GPU resources.
    void shutdown();

    // ── Registration ───────────────────────────────────────────────────────

    /**
     * @brief Register an instance for MDI rendering.
     *
     * Assigns an instance slot and creates or joins a @ref DrawBatch for the
     * given mesh + pipeline combination.  On success, @p ir.instanceId and
     * @p ir.batchId are populated.
     *
     * @param ir        Component to register; @c instanceId and @c batchId are
     *                  written on success.
     * @param mesh      GPU-uploaded mesh (provides vertex/index buffers and
     *                  AABB for frustum culling).
     * @param pipeline  Graphics pipeline handle (used as the batch key).
     * @return true on success, false if the manager is full or uninitialised.
     */
    bool registerInstance(InstancedRenderable                    &ir,
                          const ecs::component::object::Mesh     &mesh,
                          vk::Pipeline                            pipeline);

    /**
     * @brief Remove an instance.  The slot and batch membership are freed.
     *
     * No-op if @p instanceId is out of range or not active.
     */
    void unregisterInstance(uint32_t instanceId);

    // ── Per-frame ──────────────────────────────────────────────────────────

    /**
     * @brief Upload dirty instance data and batch descriptors from CPU to GPU.
     *
     * Must be called once per frame before @ref buildIndirectCommands.
     */
    void uploadInstances(uint32_t frameIndex);

    /**
     * @brief Dispatch the @c indirect_build.slang compute shader.
     *
     * Fills the indirect draw buffer for @p frameIndex with one
     * @c VkDrawIndexedIndirectCommand per DrawBatch.
     *
     * @param cmd        Active compute (or transfer-capable) command buffer.
     * @param frameIndex Current frame-in-flight index.
     */
    void buildIndirectCommands(vk::CommandBuffer cmd,
                               uint32_t          frameIndex) const;

    /**
     * @brief Issue @c vkCmdDrawIndexedIndirect for all DrawBatches.
     *
     * For each batch:
     *  1. Binds the batch vertex buffer (position SSBO bound at binding 0).
     *  2. Binds the batch index buffer.
     *  3. Binds the batch's graphics pipeline.
     *  4. Calls @c vkCmdDrawIndexedIndirect (one command from the indirect
     *     draw buffer at the batch's command slot).
     *
     * The caller is responsible for setting up the render pass, viewport,
     * scissor, and any non-pipeline descriptor sets before calling @ref draw.
     */
    void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const;

    // ── Accessors ──────────────────────────────────────────────────────────

    /// The indirect draw command buffer for @p frameIndex.
    /// Passed to FrustumCullManager so the cull shader can zero instanceCount.
    [[nodiscard]] vk::Buffer getIndirectBuffer(uint32_t frameIndex) const;

    /// The GPU instance data buffer for @p frameIndex.
    [[nodiscard]] vk::Buffer getInstanceBuffer(uint32_t frameIndex) const;

    /// The GPU batch-descriptor buffer for @p frameIndex.
    [[nodiscard]] vk::Buffer getBatchDescriptorBuffer(uint32_t frameIndex) const;

    /// Number of currently active DrawBatches.
    [[nodiscard]] uint32_t getBatchCount() const;

    [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
    // ── Batch management ────────────────────────────────────────────────────

    struct BatchKey {
        vk::Buffer   vertexBuffer{};
        vk::Buffer   indexBuffer{};
        vk::Pipeline pipeline{};

        bool operator==(const BatchKey &o) const noexcept {
            return vertexBuffer == o.vertexBuffer &&
                   indexBuffer  == o.indexBuffer  &&
                   pipeline     == o.pipeline;
        }
    };

    struct BatchKeyHash {
        std::size_t operator()(const BatchKey &k) const noexcept;
    };

    struct BatchEntry {
        BatchKey           key;
        GPUBatchDescriptor descriptor;          ///< CPU-side (uploaded each frame)
        std::vector<uint32_t> instanceSlots;    ///< Instance IDs in this batch
        bool               descriptorDirty = true;
    };

    std::vector<BatchEntry>                              batches_;
    std::unordered_map<BatchKey, uint32_t, BatchKeyHash> batchIndex_;

    uint32_t findOrCreateBatch(const BatchKey                     &key,
                               const ecs::component::object::Mesh &mesh);

    // ── Instance storage ────────────────────────────────────────────────────

    std::vector<GPUInstanceData> instances_;   ///< CPU-side slot array
    std::vector<bool>            active_;      ///< Active flag per slot
    std::vector<bool>            dirty_;       ///< Dirty flag per slot
    std::vector<uint32_t>        freeList_;    ///< Recycled instance slot IDs
    mutable std::mutex           mutex_;

    // ── GPU resources (one set per frame-in-flight) ─────────────────────────

    struct FrameResources {
        device::AllocatedBuffer  instanceBuffer;      ///< GPUInstanceData[kMaxInstances]
        device::AllocatedBuffer  batchDescriptorBuf;  ///< GPUBatchDescriptor[kMaxBatches]
        device::AllocatedBuffer  indirectBuffer;      ///< VkDrawIndexedIndirectCommand[kMaxBatches]
    };
    std::vector<FrameResources>          frames_;
    std::vector<vk::raii::DescriptorSet> computeSets_; ///< One per frame (indirect_build)

    // ── Compute pipeline (indirect_build.slang) ─────────────────────────────

    std::unique_ptr<vk::raii::DescriptorSetLayout> computeSetLayout_;
    std::unique_ptr<vk::raii::DescriptorPool>      computePool_;
    std::unique_ptr<vk::raii::PipelineLayout>      computePipelineLayout_;
    std::unique_ptr<vk::raii::Pipeline>            computePipeline_;

    device::GPUDevice    *device_        = nullptr;
    device::VMAAllocator *allocator_     = nullptr;
    uint32_t              framesInFlight_ = 0;
    bool                  initialized_   = false;
};

} // namespace window

#endif // INSTANCED_RENDERABLE_H_
