#ifndef OBJECT_H_
#define OBJECT_H_

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

#include <glm/glm.hpp>

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
 * @brief Dimension traits: maps a compile-time dimension to
 *        the appropriate GLM vector and matrix types.
 *
 * Specializations define:
 *   - VecType: position vector
 *   - ScaleType: scale vector
 *   - MatType: model matrix (always 4×4 for the GPU)
 */
template <uint32_t Dim> struct DimensionTraits;

template <> struct DimensionTraits<1> {
  using VecType = glm::vec1;
  using ScaleType = glm::vec1;
  using RotType = float; // no rotation axis in 1D
  using MatType = glm::mat4;
};

template <> struct DimensionTraits<2> {
  using VecType = glm::vec2;
  using ScaleType = glm::vec2;
  using RotType = float; // rotation around Z only
  using MatType = glm::mat4;
};

template <> struct DimensionTraits<3> {
  using VecType = glm::vec3;
  using ScaleType = glm::vec3;
  using RotType = glm::vec3; // Euler angles
  using MatType = glm::mat4;
};

// For dimensions > 3, project down to 3D types for GPU compatibility
template <uint32_t Dim>
  requires(Dim > 3)
struct DimensionTraits<Dim> {
  using VecType = glm::vec3;
  using ScaleType = glm::vec3;
  using RotType = glm::vec3;
  using MatType = glm::mat4;
};

/**
 * @brief Generic transform for any dimension, using GLM types
 */
template <uint32_t Dim> struct Transform {
  using Traits = DimensionTraits<Dim>;
  typename Traits::VecType position{};
  typename Traits::RotType rotation{};
  typename Traits::ScaleType scale = typename Traits::ScaleType{1};
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
 * @brief Build a 4×4 model matrix from a dimension-specific transform.
 *
 * Explicit specializations for Dim = 1, 2, 3 are provided in object.cpp.
 */
template <uint32_t Dim>
WINDOW_API glm::mat4 buildModelMatrix(const Transform<Dim> &transform);

// Declare explicit specializations (defined in object.cpp)
template <> WINDOW_API glm::mat4 buildModelMatrix<1>(const Transform<1> &t);
template <> WINDOW_API glm::mat4 buildModelMatrix<2>(const Transform<2> &t);
template <> WINDOW_API glm::mat4 buildModelMatrix<3>(const Transform<3> &t);

/**
 * @brief Create a descriptor set layout for per-object data
 *
 * @param device GPU device
 * @param hasTexture Whether to include a texture sampler binding
 * @return Descriptor set layout, or nullptr on failure
 */
WINDOW_API std::unique_ptr<vk::raii::DescriptorSetLayout>
createObjectDescriptorSetLayout(device::GPUDevice &device, bool hasTexture);

/**
 * @brief Templated renderable object with dimension-appropriate transforms
 *
 * Objects live in a space matching their dimension, ensuring that e.g.
 * 2D rotations aren't applied to 3D objects and vice versa.
 *
 * Each object manages its own descriptor sets, descriptor set layout,
 * and uniform buffers, and has a reference to its material via tag.
 *
 * @tparam Dim The spatial dimension (1, 2, 3, or higher)
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
template <uint32_t Dim>
  requires(Dim >= 1)
class Object {
public:
  using TransformType = Transform<Dim>;

  static constexpr uint32_t DIMENSION = Dim;

  explicit Object(const ObjectTag &tag)
      : name_(tag.name), materialTag_(tag.materialTag) {}

  ~Object() { release(); }

  // Disable copy, enable move
  Object(const Object &) = delete;
  Object &operator=(const Object &) = delete;

  Object(Object &&other) noexcept {
    std::lock_guard<std::mutex> lock(other.objectMutex_);
    name_ = std::move(other.name_);
    materialTag_ = other.materialTag_;
    other.materialTag_ = nullptr;
    transform_ = other.transform_;
    uniformBuffers_ = std::move(other.uniformBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }

  Object &operator=(Object &&other) noexcept {
    if (this != &other) {
      std::scoped_lock lock(objectMutex_, other.objectMutex_);
      release();
      name_ = std::move(other.name_);
      materialTag_ = other.materialTag_;
      other.materialTag_ = nullptr;
      transform_ = other.transform_;
      uniformBuffers_ = std::move(other.uniformBuffers_);
      descriptorPool_ = std::move(other.descriptorPool_);
      descriptorSets_ = std::move(other.descriptorSets_);
      descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
      initialized_ = other.initialized_;
      other.initialized_ = false;
    }
    return *this;
  }

  /**
   * @brief Initialize descriptor set layout, descriptor sets, and uniform
   * buffers
   * @param allocator VMA allocator for buffer creation
   * @param device GPU device
   * @param framesInFlight Number of frames in flight
   * @param hasTexture Whether the associated material uses a texture
   * @return true if initialization succeeded, false if already initialized or
   * on failure
   */
  bool initialize(device::VMAAllocator &allocator, device::GPUDevice &device,
                  uint32_t framesInFlight, bool hasTexture = false) {
    std::lock_guard<std::mutex> lock(objectMutex_);

    if (initialized_) {
      return false;
    }

    // Create descriptor set layout (owned by the object)
    descriptorSetLayout_ =
        createObjectDescriptorSetLayout(device, hasTexture);
    if (!descriptorSetLayout_) {
      return false;
    }

    vk::DescriptorSetLayout layout = **descriptorSetLayout_;

    // Create uniform buffers (one per frame in flight)
    uniformBuffers_.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
      auto ubo = allocator.createUniformBuffer(sizeof(UniformBufferData),
                                               std::string(name_) + "_ubo_" +
                                                   std::to_string(i));
      if (!ubo.isValid()) {
        return false;
      }
      uniformBuffers_.push_back(std::move(ubo));
    }

    // Create descriptor pool
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer,
                                    framesInFlight};

    vk::DescriptorPoolCreateInfo poolInfo{
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, framesInFlight, 1,
        &poolSize};

    try {
      descriptorPool_ = std::make_unique<vk::raii::DescriptorPool>(
          device.getRaiiDevice(), poolInfo);
    } catch (const vk::SystemError &) {
      return false;
    }

    // Allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight, layout);
    vk::DescriptorSetAllocateInfo allocInfo{**descriptorPool_, framesInFlight,
                                            layouts.data()};

    try {
      auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(), allocInfo);
      descriptorSets_.reserve(sets.size());
      for (auto &s : sets) {
        descriptorSets_.push_back(std::move(s));
      }
    } catch (const vk::SystemError &) {
      return false;
    }

    // Update descriptor sets to point to uniform buffers
    for (uint32_t i = 0; i < framesInFlight; ++i) {
      vk::DescriptorBufferInfo bufferInfo{uniformBuffers_[i].getBuffer(), 0,
                                          sizeof(UniformBufferData)};

      vk::WriteDescriptorSet descriptorWrite{*descriptorSets_[i],
                                             0,
                                             0,
                                             1,
                                             vk::DescriptorType::eUniformBuffer,
                                             nullptr,
                                             &bufferInfo,
                                             nullptr};

      device.getRaiiDevice().updateDescriptorSets(descriptorWrite, {});
    }

    initialized_ = true;
    return true;
  }

  /**
   * @brief Release all GPU resources
   */
  void release() {
    descriptorSets_.clear();
    descriptorPool_.reset();
    uniformBuffers_.clear();
    descriptorSetLayout_.reset();
    initialized_ = false;
  }

  /**
   * @brief Get the descriptor set layout owned by this object
   * @return Descriptor set layout handle, or null handle if not initialized
   */
  [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const {
    std::lock_guard<std::mutex> lock(objectMutex_);
    return descriptorSetLayout_ ? **descriptorSetLayout_
                                : vk::DescriptorSetLayout{};
  }

  /**
   * @brief Update uniform buffer data for the current frame
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix
   * @param projMatrix Projection matrix
   */
  void updateUniforms(uint32_t frameIndex, const glm::mat4 &viewMatrix,
                      const glm::mat4 &projMatrix) {
    std::lock_guard<std::mutex> lock(objectMutex_);

    if (!initialized_ || frameIndex >= uniformBuffers_.size()) {
      return;
    }

    UniformBufferData uboData{};
    uboData.model = buildModelMatrix<Dim>(transform_);
    uboData.view = viewMatrix;
    uboData.proj = projMatrix;

    void *mapped = uniformBuffers_[frameIndex].map();
    if (mapped) {
      std::memcpy(mapped, &uboData, sizeof(UniformBufferData));
      uniformBuffers_[frameIndex].unmap();
    }
  }

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
            uint32_t instanceCount = 1) const {
    std::lock_guard<std::mutex> lock(objectMutex_);

    if (!initialized_ || frameIndex >= descriptorSets_.size()) {
      return;
    }

    // Bind material pipeline
    material.bind(cmd);

    // Bind descriptor set for this frame
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                           material.getPipelineLayout(), 0,
                           {*descriptorSets_[frameIndex]}, {});

    // Draw
    cmd.draw(vertexCount, instanceCount, 0, 0);
  }

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
  std::string name_;
  const MaterialTag *materialTag_ = nullptr;
  TransformType transform_{};

  // Per-frame GPU resources
  std::vector<device::AllocatedBuffer> uniformBuffers_;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout_;

  bool initialized_ = false;
  mutable std::mutex objectMutex_;
};

// Common type aliases
using Object1D = Object<1>;
using Object2D = Object<2>;
using Object3D = Object<3>;

} // namespace window

#endif // OBJECT_H_
