#ifndef OBJECT_H_
#define OBJECT_H_

#include "glm/ext/matrix_float4x4.hpp"
#include "material.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include "window_export.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace window {

/**
 * @brief Tag for identifying objects in the resource system
 *
 * Describes an object with n faces in n dimensions.
 * Each object has a base material applied to all faces.
 * Per-face material overrides can be configured at runtime.
 */
struct WINDOW_API ObjectTag {
  const char *name;
  const MaterialTag *baseMaterialTag; // Base material for all faces
  uint32_t dimension;                 // Spatial dimension (1, 2, 3, ...)
  uint32_t faceCount;                 // Number of faces in the object

  constexpr ObjectTag(const char *n, const MaterialTag *baseMat, uint32_t dim,
                      uint32_t faces)
      : name(n), baseMaterialTag(baseMat), dimension(dim), faceCount(faces) {}
};

/**
 * @brief Uniform buffer data matching the shader UBO layout
 */
struct WINDOW_API UniformBufferData {
  glm::mat4 model{1.0f};
  glm::mat4 view{1.0f};
  glm::mat4 proj{1.0f};
};

/**
 * @brief A single face of an object with its own material binding
 *
 * Each face starts with the object's base material. An override material
 * can be assigned per-face for multi-material objects.
 */
struct WINDOW_API Face {
  uint32_t faceIndex = 0;
  uint32_t vertexOffset = 0;   // First vertex of this face
  uint32_t vertexCount = 0;    // Number of vertices in this face
  Material *overrideMaterial = nullptr; // nullptr = use base material

  /**
   * @brief Get the effective material for this face
   * @param baseMaterial The object's base material
   * @return The material to use for rendering this face
   */
  [[nodiscard]] Material *getEffectiveMaterial(Material *baseMaterial) const {
    return overrideMaterial ? overrideMaterial : baseMaterial;
  }
};

/**
 * @brief Renderable object with n dimensions and n faces
 *
 * Objects live in a space of configurable dimension, with transform
 * arrays sized accordingly. Each object has:
 * - A base material applied to all faces
 * - Optional per-face material overrides
 * - Per-object pipeline, descriptor sets, and uniform buffers
 *
 * The uniform buffer size is determined by the object's dimension,
 * always producing a 4x4 model matrix for the GPU while computing
 * transforms dimension-appropriately on the CPU.
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
class WINDOW_API Object {
public:
  explicit Object(const ObjectTag &tag);
  ~Object();

  // Disable copy, enable move
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;
  Object(Object &&other) noexcept;
  Object &operator=(Object &&other) noexcept;

  /**
   * @brief Initialize descriptor sets, uniform buffers, and per-object pipeline
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
   * @brief Set an override material for a specific face
   * @param faceIndex Index of the face to override
   * @param material Material to use (nullptr to revert to base)
   * @return true if the face index is valid
   */
  bool setFaceMaterial(uint32_t faceIndex, Material *material);

  /**
   * @brief Configure a face's vertex range
   * @param faceIndex Index of the face
   * @param vertexOffset First vertex index
   * @param vertexCount Number of vertices
   * @return true if the face index is valid
   */
  bool setFaceVertices(uint32_t faceIndex, uint32_t vertexOffset,
                       uint32_t vertexCount);

  /**
   * @brief Update uniform buffer data for the current frame
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix (glm::mat4)
   * @param projMatrix Projection matrix (glm::mat4)
   */
  void updateUniforms(uint32_t frameIndex, const glm::mat4 &viewMatrix,
                      const glm::mat4 &projMatrix);

  /**
   * @brief Draw all faces of this object
   *
   * Binds the per-object pipeline and descriptor sets, then draws
   * each face. Faces using the base material are batched together.
   *
   * @param cmd Command buffer to record draw commands
   * @param frameIndex Current frame in flight index
   */
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex) const;

  // =========================================================================
  // Transform accessors (dimension-agnostic using vectors)
  // =========================================================================

  void setPosition(const std::vector<float> &pos);
  void setRotation(const std::vector<float> &rot);
  void setScale(const std::vector<float> &scl);

  [[nodiscard]] std::vector<float> getPosition() const;
  [[nodiscard]] std::vector<float> getRotation() const;
  [[nodiscard]] std::vector<float> getScale() const;

  // Getters
  [[nodiscard]] const char *getName() const { return name_; }
  [[nodiscard]] const MaterialTag *getBaseMaterialTag() const {
    return baseMaterialTag_;
  }
  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] uint32_t getDimension() const { return dimension_; }
  [[nodiscard]] uint32_t getFaceCount() const {
    return static_cast<uint32_t>(faces_.size());
  }
  [[nodiscard]] const Face *getFace(uint32_t index) const;

private:
  /**
   * @brief Build model matrix from transform (dimension-agnostic using glm)
   * @return 4x4 model matrix
   */
  glm::mat4 buildModelMatrix() const;

  const char *name_;
  const MaterialTag *baseMaterialTag_ = nullptr;
  uint32_t dimension_ = 3;

  // Dimension-sized transform vectors
  std::vector<float> position_;
  std::vector<float> rotation_;
  std::vector<float> scale_;

  // Per-face data
  std::vector<Face> faces_;

  // Per-object pipeline (created by base material, owned by object)
  ObjectPipeline objectPipeline_;

  // Per-frame GPU resources
  std::vector<device::AllocatedBuffer> uniformBuffers_;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout_;

  bool initialized_ = false;
  mutable std::mutex objectMutex_;
};

} // namespace window

#endif // OBJECT_H_
