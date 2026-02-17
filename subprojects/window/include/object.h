#ifndef OBJECT_H_
#define OBJECT_H_

#include "material.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include "window_export.h"
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>
#include <mutex>
#include <string>

namespace window {

/**
 * @brief Tag for identifying objects in the resource system
 */
struct WINDOW_API ObjectTag {
  const char *name;
  const MaterialTag *materialTag = nullptr;
  uint32_t dimension = 3; // Default 3D

  constexpr ObjectTag(const char *n, const MaterialTag *mat, uint32_t dim = 3)
      : name(n), materialTag(mat), dimension(dim) {}
};

/**
 * @brief Transform data define dimension with template (position, rotation,
 * scale)
 * 0-x 1-y 2-z ...
 */
template <uint32_t Dim> struct WINDOW_API Transform {
  std::array<float, Dim> position = {};
  std::array<float, Dim> rotation = {};
  std::array<float, Dim> scale;

  Transform() { scale.fill(1.0f); }
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
 * @brief Templated renderable object with dimension-appropriate transforms
 *
 * Objects live in a space matching their dimension, ensuring that e.g.
 * 2D rotations aren't applied to 3D objects and vice versa.
 *
 * Each object manages its own descriptor sets, uniform buffers, and pipeline.
 * The pipeline is created by the material but stored per-object, preventing
 * descriptor set conflicts when multiple objects share the same material.
 *
 * @tparam Dim The spatial dimension (1, 2, 3, or higher)
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
template <uint32_t Dim> class Object {
public:
  using TransformType = Transform<Dim>;
  static constexpr uint32_t DIMENSION = Dim;

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
   * @param material Material to create pipeline from
   * @param renderPass Render pass for pipeline compatibility
   * @param framesInFlight Number of frames in flight
   * @param pipelineConfig Pipeline configuration
   * @return true if initialization succeeded
   */
  bool initialize(device::VMAAllocator &allocator, device::GPUDevice &device,
                  Material &material, vk::RenderPass renderPass,
                  uint32_t framesInFlight,
                  const PipelineConfig &pipelineConfig = {});

  /**
   * @brief Release all GPU resources
   */
  void release();

  /**
   * @brief Update uniform buffer data for the current frame
   *
   * Uses glm matrices for dimension-agnostic transform computation.
   *
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix (glm::mat4)
   * @param projMatrix Projection matrix (glm::mat4)
   */
  void updateUniforms(uint32_t frameIndex, const glm::mat4 &viewMatrix,
                      const glm::mat4 &projMatrix);

  /**
   * @brief Draw this object using its own pipeline and descriptor sets
   *
   * Supports multi-GPU by accepting a command buffer that may be associated
   * with any device queue. Uses per-object pipeline and descriptor sets
   * to avoid conflicts with other objects using the same material.
   *
   * @param cmd Command buffer to record draw commands
   * @param frameIndex Current frame in flight index
   * @param vertexCount Number of vertices to draw
   * @param instanceCount Number of instances to draw
   */
  void draw(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t vertexCount,
            uint32_t instanceCount = 1) const;

  // Transform accessors
  void setTransform(const TransformType &transform) {
    std::lock_guard<std::mutex> lock(objectMutex_);
    transform_ = transform;
  }

  [[nodiscard]] TransformType getTransform() const {
    std::lock_guard<std::mutex> lock(objectMutex_);
    return transform_;
  }

  /**
   * @brief Set position (dimension-agnostic)
   */
  void setPosition(const std::array<float, Dim> &pos) {
    std::lock_guard<std::mutex> lock(objectMutex_);
    transform_.position = pos;
  }

  /**
   * @brief Set rotation angles in radians (dimension-agnostic)
   */
  void setRotation(const std::array<float, Dim> &rot) {
    std::lock_guard<std::mutex> lock(objectMutex_);
    transform_.rotation = rot;
  }

  /**
   * @brief Set scale factors (dimension-agnostic)
   */
  void setScale(const std::array<float, Dim> &scl) {
    std::lock_guard<std::mutex> lock(objectMutex_);
    transform_.scale = scl;
  }

  // Getters
  [[nodiscard]] const std::string &getName() const { return name_; }
  [[nodiscard]] const MaterialTag *getMaterialTag() const {
    return materialTag_;
  }
  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] static constexpr uint32_t getDimension() { return Dim; }

private:
  /**
   * @brief Build model matrix from transform (dimension-agnostic using glm)
   * @return 4x4 model matrix
   */
  glm::mat4 buildModelMatrix() const;

  std::string name_;
  const MaterialTag *materialTag_ = nullptr;
  TransformType transform_{};

  // Per-object pipeline (created by material, owned by object)
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

#include "../src/object_imp.hpp"

#endif // OBJECT_H_
