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
#include <gbm.h>
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
  std::array<float, Dim> position;
  std::array<float, Dim> rotation;
  std::array<float, Dim> scale;
};

/**
 * @brief Uniform buffer data matching the shader UBO layout
 */
struct WINDOW_API UniformBufferData {
  // 4x4 matrices stored as float[16] in column-major order
  float model[16];
  float view[16];
  float proj[16];
};

/**
 * @brief Templated renderable object with dimension-appropriate transforms
 *
 * Objects live in a space matching their dimension, ensuring that e.g.
 * 2D rotations aren't applied to 3D objects and vice versa.
 *
 * Each object manages its own descriptor sets and uniform buffers,
 * and has a reference to its material via tag.
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
   * @brief Initialize descriptor sets and uniform buffers
   * @param allocator VMA allocator for buffer creation
   * @param device GPU device
   * @param descriptorSetLayout Layout from the material
   * @param framesInFlight Number of frames in flight
   * @return true if initialization succeeded, false if already initialized or
   * on failure
   */
  bool initialize(device::VMAAllocator &allocator, device::GPUDevice &device,
                  vk::DescriptorSetLayout descriptorSetLayout,
                  uint32_t framesInFlight);

  /**
   * @brief Release all GPU resources
   */
  void release();

  /**
   * @brief Update uniform buffer data for the current frame
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix (16 floats, column-major)
   * @param projMatrix Projection matrix (16 floats, column-major)
   */
  void updateUniforms(uint32_t frameIndex, const float *viewMatrix,
                      const float *projMatrix);

  /**
   * @brief Draw this object
   * @param cmd Command buffer to record draw commands
   * @param material Material to use for rendering
   * @param frameIndex Current frame in flight index
   * @param vertexCount Number of vertices to draw
   * @param instanceCount Number of instances to draw
   */
  void draw(vk::CommandBuffer cmd, const Material &material,
            uint32_t frameIndex, uint32_t vertexCount,
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

  // Getters
  [[nodiscard]] const std::string &getName() const { return name_; }
  [[nodiscard]] const MaterialTag *getMaterialTag() const {
    return materialTag_;
  }
  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] static constexpr uint32_t getDimension() { return Dim; }

private:
  static void setIdentity(float *matrix) {
    std::memset(matrix, 0, sizeof(float) * 16);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
  }

  /**
   * @brief Build model matrix from transform (dimension-specific)
   */
  void buildModelMatrix(float *matrix) const;

  std::string name_;
  const MaterialTag *materialTag_ = nullptr;
  TransformType transform_{};

  // Per-frame GPU resources
  std::vector<device::AllocatedBuffer> uniformBuffers_;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;

  bool initialized_ = false;
  mutable std::mutex objectMutex_;
};

} // namespace window

#include "../src/object_imp.hpp"

#endif // OBJECT_H_
