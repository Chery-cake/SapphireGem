#ifndef OBJECT_H_
#define OBJECT_H_

#include "glm/ext/matrix_float4x4.hpp"
#include "material.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include "window_export.h"
#include <array>
#include <cstdint>
#include <gbm.h>
#include <memory>
#include <mutex>
#include <string>

namespace window {

/**
 * @brief Tag for identifying objects in the resource system
 *
 * Describes an object with a base material in a given dimension.
 * The face count is no longer specified here — it is computed
 * automatically from the vertices/indices provided to the Object.
 *
 * @tparam Dim is embedded in the tag for type-safety documentation,
 *         but the actual dimension enforcement is via Object<Dim>.
 */
struct WINDOW_API ObjectTag {
  const char *name;
  const MaterialTag *baseMaterialTag; // Base material for all faces

  constexpr ObjectTag(const char *n, const MaterialTag *baseMat)
      : name(n), baseMaterialTag(baseMat) {}
};

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
  static_assert(Dim >= 1 && Dim <= 3,
                "Dimension must be 1, 2, or 3 (GLM supports up to mat4)");

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

/**
 * @brief A single face (triangle) of an object
 *
 * Tracks the vertex offset and count for draw calls.
 * Rendering mode diversity is handled per-vertex in the shader,
 * not by per-face material overrides.
 */
struct WINDOW_API Face {
  uint32_t faceIndex = 0;
  uint32_t vertexOffset = 0; // First vertex of this face
  uint32_t vertexCount = 0;  // Number of vertices in this face
};

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

/**
 * @brief Templated renderable object with compile-time dimension
 *
 * A 2D object (Object<2>) cannot interact directly with a 3D object
 * (Object<3>) since they are different types. This prevents accidental
 * mixing of dimensions.
 *
 * Objects receive their vertices and indices at construction, and the
 * face count is calculated automatically from the index data (every
 * 3 consecutive indices form one triangular face).
 *
 * The uniform buffer uses (Dim+1)×(Dim+1) matrices, so a 2D object
 * has a smaller GPU buffer compared to a 3D object.
 *
 * All transform calculations (position, rotation, scale) are
 * dimension-agnostic: the same operations apply across all dimensions,
 * just on arrays of different sizes.
 *
 * Rendering mode diversity (different styles per face) is handled
 * per-vertex in the shader via mode indices, not by per-face material
 * overrides. Each object uses a single material/pipeline instance.
 *
 * @tparam Dim Spatial dimension (1, 2, 3, ...)
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
template <uint32_t Dim> class Object {
public:
  static constexpr uint32_t DIMENSION = Dim;
  using UBO = UniformBufferData<Dim>;
  using GPUUBO = GPUUniformBufferData<Dim>;
  using MatType = typename UBO::MatType;
  using VertexType = Vertex<Dim>;

  /**
   * @brief Construct an Object from tag, vertices and indices
   *
   * Faces are calculated automatically from the index data.
   * Every 3 consecutive indices form one triangular face.
   *
   * @param tag Object tag (name, base material)
   * @param vertices Vertex data
   * @param indices Index data (must be a multiple of 3)
   */
  Object(const ObjectTag &tag, std::vector<VertexType> vertices,
         std::vector<uint32_t> indices);

  ~Object();

  // Disable copy, enable move
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  Object(Object &&other) noexcept;
  Object &operator=(Object &&other) noexcept;

  /**
   * @brief Initialize descriptor sets, uniform buffers, and per-object
   * pipeline
   * @param allocator VMA allocator for buffer creation
   * @param device GPU device
   * @param baseMaterial Material to use as the base for all faces
   * @param renderPass Render pass for pipeline compatibility
   * @param framesInFlight Number of frames in flight
   * @param pipelineConfig Pipeline configuration
   * @return true if initialization succeeded
   */
  bool initialize(device::VMAAllocator &allocator, device::GPUDevice &device,
                  Material &baseMaterial, vk::RenderPass renderPass,
                  uint32_t framesInFlight,
                  const PipelineConfig &pipelineConfig = {});

  /**
   * @brief Release all GPU resources
   */
  void release();

  /**
   * @brief Set the time value for push constant animation
   * @param time Time value (typically in seconds)
   */
  void setTime(float time);

  /**
   * @brief Update uniform buffer data for the current frame
   *
   * Uses dimension-appropriate matrices.
   *
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix
   * @param projMatrix Projection matrix
   */
  void updateUniforms(uint32_t frameIndex, const MatType &viewMatrix,
                      const MatType &projMatrix);

  /**
   * @brief Draw all faces of this object
   *
   * Binds the per-object pipeline and descriptor sets, then issues
   * a single draw call for all vertices.
   *
   * @param cmd Command buffer to record draw commands
   * @param frameIndex Current frame in flight index
   */
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const;

  // =========================================================================
  // Transform accessors (dimension-agnostic using fixed-size arrays)
  // =========================================================================

  void setPosition(const std::array<float, Dim> &pos);
  void setRotation(const std::array<float, Dim> &rot);
  void setScale(const std::array<float, Dim> &scl);

  [[nodiscard]] std::array<float, Dim> getPosition() const;
  [[nodiscard]] std::array<float, Dim> getRotation() const;
  [[nodiscard]] std::array<float, Dim> getScale() const;

  // Getters
  [[nodiscard]] const char *getName() const { return name_; }
  [[nodiscard]] const MaterialTag *getBaseMaterialTag() const {
    return baseMaterialTag_;
  }
  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] static constexpr uint32_t getDimension() { return Dim; }
  [[nodiscard]] uint32_t getFaceCount() const {
    return static_cast<uint32_t>(faces_.size());
  }
  [[nodiscard]] const Face *getFace(uint32_t index) const;
  [[nodiscard]] uint32_t getTotalVertexCount() const {
    return static_cast<uint32_t>(vertices_.size());
  }

private:
  /**
   * @brief Build model matrix from transform (dimension-agnostic)
   *
   * Constructs a (Dim+1)×(Dim+1) homogeneous transformation matrix
   * from position, rotation, and scale arrays. The same algorithm
   * applies identically across all dimensions:
   * - Translation: sets the last column
   * - Scale: diagonal entries
   * - Rotation: applies rotation in each axis-pair plane
   *
   * @return (Dim+1)×(Dim+1) model matrix
   */
  MatType buildModelMatrix() const;

  /**
   * @brief Calculate faces from indices
   *
   * Every 3 consecutive indices form one triangular face.
   */
  void calculateFaces();

  const char *name_;
  const MaterialTag *baseMaterialTag_ = nullptr;

  // Transform (fixed-size arrays, dimension-agnostic)
  std::array<float, Dim> position_{};
  std::array<float, Dim> rotation_{};
  std::array<float, Dim> scale_{};

  // Geometry data
  std::vector<VertexType> vertices_;
  std::vector<uint32_t> indices_;

  // Auto-calculated faces
  std::vector<Face> faces_;

  // Per-object pipeline (created by material, owned by object)
  ObjectPipeline objectPipeline_;

  // Pipeline config (for push constants)
  PipelineConfig pipelineConfig_;

  // Push constant data
  float time_ = 0.0f;

  // Per-frame GPU resources
  std::vector<device::AllocatedBuffer> uniformBuffers_;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout_;

  bool initialized_ = false;
  mutable std::mutex objectMutex_;
};

} // namespace window

// Template implementation
#include "../src/object_impl.hpp"

#endif // OBJECT_H_
