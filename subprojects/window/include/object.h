#ifndef OBJECT_H_
#define OBJECT_H_

#include "bindless_types.h"
#include "component_registry.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "material.h"
#include "mesh.h"
#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <tuple>
#include <vector>

namespace ecs::component::object {

/**
 * @brief Dimension-sized uniform buffer data matching the shader UBO layout
 *
 * For Dim=2: uses 3×3 matrices (9 floats each, padded to std140: 3×vec4 = 48
 * bytes each) For Dim=3: uses 4×4 matrices (16 floats each = 64 bytes each)
 *
 * The matrix size is (Dim+1)×(Dim+1) to support homogeneous coordinates.
 *
 * @tparam Dim Spatial dimension (1, 2, 3, ...)
 */
template <uint32_t Dim> struct UniformBufferData {
  // Matrix type: (Dim+1) × (Dim+1) for homogeneous coordinates
  using MatType = glm::mat<Dim + 1, Dim + 1, float, glm::defaultp>;

  MatType model{1.0f};
  MatType view{1.0f};
  MatType proj{1.0f};
};

/**
 * @brief GPU-compatible UBO layout with std140 padding
 *
 * In std140 layout, each column of a matrix is aligned to 16 bytes (vec4).
 * For Dim=3 (mat4): columns are vec4, naturally 16-byte aligned — no padding.
 * For Dim=2 (mat3): columns are vec3 (12 bytes) but must be padded to 16 bytes.
 *
 * This struct ensures the CPU-side data matches the GPU shader UBO layout.
 *
 * @tparam Dim Spatial dimension
 */
template <uint32_t Dim> struct GPUUniformBufferData {
  // Default: same layout as UniformBufferData (works for Dim=3/mat4)
  using UBO = UniformBufferData<Dim>;
  using MatType = typename UBO::MatType;
  MatType model{1.0f};
  MatType view{1.0f};
  MatType proj{1.0f};

  void fromUBO(const UBO &ubo) {
    model = ubo.model;
    view = ubo.view;
    proj = ubo.proj;
  }
};

/**
 * @brief Specialization for 2D: pads mat3 columns to 16-byte alignment
 *
 * Each column of float3x3 in std140 is stored as (vec3 + 4 bytes padding),
 * giving 48 bytes per matrix (3 columns × 16 bytes).
 * Total UBO size: 3 matrices × 48 bytes = 144 bytes.
 */
template <> struct GPUUniformBufferData<2> {
  using UBO = UniformBufferData<2>;
  using MatType = typename UBO::MatType;

  struct Std140Column {
    float x, y, z;
    float pad{0.0f};
  };

  struct Std140Mat3 {
    Std140Column col[3];
  };

  Std140Mat3 model{};
  Std140Mat3 view{};
  Std140Mat3 proj{};

  static Std140Mat3 fromMat3(const glm::mat3 &m) {
    Std140Mat3 result{};
    for (int c = 0; c < 3; ++c) {
      result.col[c].x = m[c][0];
      result.col[c].y = m[c][1];
      result.col[c].z = m[c][2];
      result.col[c].pad = 0.0f;
    }
    return result;
  }

  void fromUBO(const UBO &ubo) {
    model = fromMat3(ubo.model);
    view = fromMat3(ubo.view);
    proj = fromMat3(ubo.proj);
  }
};

// Face is defined in mesh.h (included above)

/**
 * @brief Vertex type for Dim-dimensional objects
 *
 * Position has Dim components, color always has 3 (RGB).
 *
 * @tparam Dim Spatial dimension
 */
template <uint32_t Dim> struct Vertex {
  std::array<float, Dim> position;
  std::array<float, 3> color; // RGB TODO change to RGBA
};

template <uint32_t Dim> struct TransformComponent {
  std::array<float, Dim> position{};
  std::array<float, Dim> rotation{};
  std::array<float, Dim> scale{1};

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
  using MatType = glm::mat<Dim + 1, Dim + 1, float, glm::defaultp>;
  MatType modelMatrix() const;

  template <uint32_t FromDim, uint32_t ToDim>
  static TransformComponent<ToDim>
  projectDown(const TransformComponent<FromDim> &src);
};

/**
 * @brief Abstract interface for any renderable component.
 *
 * Subclasses implement draw.  Compute/pre-render work is now driven by
 * @ref AsyncComputeManager via explicit record callbacks rather than an
 * inline virtual method.
 */
class RenderComponentBase {
public:
  virtual ~RenderComponentBase() = default;

  /**
   * @brief Issue draw commands into the command buffer.
   * @param cmd         Active command buffer (inside a render pass).
   * @param frameIndex  Current frame‑in‑flight index (for resource
   * selection).
   */
  virtual void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const = 0;
};

/**
 * @brief Dimension-agnostic renderable component.
 *
 * Uses the non-templated @ref Mesh resource.  The spatial dimension is
 * read from @ref Mesh::dimension at initialisation time and stored
 * internally.  @ref updateUniforms is overloaded for 2-D (mat3) and
 * 3-D (mat4) callers so the entity's TransformComponent can pass its
 * model matrix directly without any manual conversion.
 *
 * @c RenderComponent additionally inherits from
 * @ref ecs::component::ComponentRegistry<RenderComponent> so that all live
 * instances can be queried globally without going through per-scene
 * @ref window::RenderWorld objects:
 *
 * @code
 *   // Iterate every RenderComponent across all scenes:
 *   RenderComponent::forEach([](RenderComponent &rc) {
 *       if (rc.initialized) { ... }
 *   });
 * @endcode
 */
struct RenderComponent
    : public RenderComponentBase
    , public ecs::component::ComponentRegistry<RenderComponent> {
  // Pipeline & descriptors
  std::shared_ptr<window::ObjectPipeline> pipeline;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool;
  std::vector<vk::raii::DescriptorSet> descriptorSets;

  // Buffers
  std::vector<device::AllocatedBuffer> uniformBuffers;
  std::vector<device::AllocatedBuffer> faceDataBuffers;
  std::vector<device::AllocatedBuffer> displacedPositionBuffers;

  // Compute
  std::unique_ptr<vk::raii::Pipeline> computeUpdatePipeline;

  // Bindless links
  vk::DescriptorSet bindlessDescriptorSet{};
  device::TextureId baseTextureId{};

  window::PipelineConfig pipelineConfig;
  float time = 0.0F;

  // Indirect draw
  device::AllocatedBuffer indirectDrawBuffer;
  vk::DrawIndexedIndirectCommand indirectDrawCmd{};
  mutable bool indirectCommandDirty = true;

  bool initialized = false;

  // ── Lifecycle ──────────────────────────────────────────────────────────
  bool initialize(device::VMAAllocator &allocator, device::GPUDevice &device,
                  const Mesh &mesh, const window::Material &baseMaterial,
                  vk::RenderPass renderPass, uint32_t framesInFlight,
                  const window::PipelineConfig &config,
                  vk::DescriptorSetLayout bindlessLayout);
  void release();

  bool initializeCompute(device::GPUDevice &device,
                         device::ShaderManager &shader,
                         const device::ShaderTag *computeUpdateTag,
                         const device::ShaderTag *computeNormalTag,
                         uint32_t vertexCount, uint32_t indexCount);

  // ── Drawing & uniform updates ──────────────────────────────────────────
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const override;

  /**
   * @brief Record the compute dispatch commands for this component.
   *
   * This is the logic previously in @c preRender, now exposed as a plain
   * method so that @ref AsyncComputeManager can call it from a dedicated
   * compute command buffer.  Pass a lambda wrapping this method as the
   * @c recordFn when calling @ref AsyncComputeManager::registerEffect.
   *
   * No-op if no compute pipeline has been set up via @ref initializeCompute.
   */
  void recordComputeCommands(vk::CommandBuffer cmd,
                             uint32_t frameIndex) const;

  /// Update UBO for a 3-D entity (uses mat4 model / view / proj).
  void updateUniforms(uint32_t frameIndex,
                      const glm::mat4 &model,
                      const glm::mat4 &view,
                      const glm::mat4 &proj);

  /// Update UBO for a 2-D entity (uses mat3 model / view / proj).
  void updateUniforms(uint32_t frameIndex,
                      const glm::mat3 &model,
                      const glm::mat3 &view,
                      const glm::mat3 &proj);

  // ── Double-buffered displaced positions (§4.2) ─────────────────────────
  /**
   * @brief Advance the write slot after compute has been submitted.
   *
   * Call this once per frame after @ref AsyncComputeManager::executeFrame to
   * record the timeline semaphore value that will signal completion of the
   * current write, then toggle the write index.
   *
   * @param signalValue  The timeline semaphore value that will be signalled
   *                     when the current compute submission completes.
   */
  void advanceWriteBuffer(uint64_t signalValue);

  /**
   * @brief Update @c readIndex_ to the latest confirmed write buffer.
   *
   * Queries the current timeline semaphore counter (non-blocking CPU read) and
   * advances @c readIndex_ to the most recently completed write slot.  Call
   * before @ref draw to ensure the freshest safe data is used.
   *
   * No-op if the compute semaphore handle is null.
   *
   * @param device       Vulkan logical device (for the query).
   * @param timeline     Timeline semaphore to query.
   */
  void updateReadBuffer(vk::Device device, vk::Semaphore timeline);

  // ── LOD mesh swap ─────────────────────────────────────────────────────
  /**
   * @brief Swap the active mesh without reallocating GPU resources.
   *
   * Called by @ref window::LODSystem::update to switch between LOD levels.
   * The replacement mesh must have been GPU-uploaded and must share the same
   * vertex-buffer layout as the mesh used during @ref initialize.
   *
   * @param mesh  New active mesh.  Lifetime must exceed the next draw call.
   */
  void setMesh(const Mesh *mesh) { mesh_ = mesh; }

  // ── Per-face material ──────────────────────────────────────────────────
  void setFaceMaterial(uint32_t faceIndex, const device::FaceMaterial &desc,
                       size_t faceCount);
  device::FaceMaterial getFaceMaterial(uint32_t faceIndex) const;

private:
  uint32_t dimension_ = 3;         ///< Copied from Mesh::dimension at init
  const Mesh *mesh_   = nullptr;

  std::vector<device::FaceMaterial> faceMaterials;

  mutable std::recursive_mutex mutex_;

  // ── Double-buffered displaced positions (§4.2) ─────────────────────────
  /// Slot currently being written by compute (0 or 1); mirrors frameIndex.
  uint32_t writeIndex_ = 0;
  /// Last confirmed completed write slot; used to select the safe read buffer.
  uint32_t readIndex_ = 0;
  /// Timeline semaphore values that signal completion of each write slot.
  uint64_t completedSemaphoreValues_[2] = {0, 0};

  void uploadFaceData(uint32_t frameIndex, size_t faceCount) const;
  void uploadIndirectCommand() const;
};

} // namespace ecs::component::object

// Template implementation
#include "../src/object_impl.hpp"

#endif // OBJECT_H_
