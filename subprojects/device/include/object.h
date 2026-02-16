#ifndef OBJECT_H_
#define OBJECT_H_

#include "device_export.h"
#include "material.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace device {

/**
 * @brief Tag for identifying objects in the resource system
 */
struct DEVICE_API ObjectTag {
  const char *name;
  const MaterialTag *materialTag = nullptr;
  uint32_t dimension = 3; // Default 3D

  constexpr ObjectTag(const char *n, const MaterialTag *mat, uint32_t dim = 3)
      : name(n), materialTag(mat), dimension(dim) {}
};

/**
 * @brief Transform data for a 1D object (position only on a line)
 */
struct DEVICE_API Transform1D {
  float position = 0.0f;
  float scale = 1.0f;
};

/**
 * @brief Transform data for a 2D object (position, rotation around Z, scale)
 */
struct DEVICE_API Transform2D {
  float positionX = 0.0f;
  float positionY = 0.0f;
  float rotation = 0.0f; // Rotation in radians (around Z axis only)
  float scaleX = 1.0f;
  float scaleY = 1.0f;
};

/**
 * @brief Transform data for a 3D object (full position, rotation, scale)
 */
struct DEVICE_API Transform3D {
  float positionX = 0.0f;
  float positionY = 0.0f;
  float positionZ = 0.0f;
  float rotationX = 0.0f; // Euler angles in radians
  float rotationY = 0.0f;
  float rotationZ = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float scaleZ = 1.0f;
};

/**
 * @brief Uniform buffer data matching the shader UBO layout
 */
struct DEVICE_API UniformBufferData {
  // 4x4 matrices stored as float[16] in column-major order
  float model[16];
  float view[16];
  float proj[16];
};

/**
 * @brief Selects the appropriate transform type for a given dimension
 */
template <uint32_t Dim> struct TransformSelector;

template <> struct TransformSelector<1> { using type = Transform1D; };
template <> struct TransformSelector<2> { using type = Transform2D; };
template <> struct TransformSelector<3> { using type = Transform3D; };

// For dimensions > 3, use 3D transform as the base
template <uint32_t Dim>
  requires(Dim > 3)
struct TransformSelector<Dim> {
  using type = Transform3D;
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
template <uint32_t Dim>
  requires(Dim >= 1)
class Object {
public:
  using TransformType = typename TransformSelector<Dim>::type;

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
      initialized_ = other.initialized_;
      other.initialized_ = false;
    }
    return *this;
  }

  /**
   * @brief Initialize descriptor sets and uniform buffers
   * @param allocator VMA allocator for buffer creation
   * @param device GPU device
   * @param descriptorSetLayout Layout from the material
   * @param framesInFlight Number of frames in flight
   * @return true if initialization succeeded, false if already initialized or on failure
   */
  bool initialize(VMAAllocator &allocator, GPUDevice &device,
                  vk::DescriptorSetLayout descriptorSetLayout,
                  uint32_t framesInFlight) {
    std::lock_guard<std::mutex> lock(objectMutex_);

    if (initialized_) {
      return false; // Already initialized - call release() first to reinitialize
    }

    // Create uniform buffers (one per frame in flight)
    uniformBuffers_.reserve(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i) {
      auto ubo = allocator.createUniformBuffer(
          sizeof(UniformBufferData),
          std::string(name_) + "_ubo_" + std::to_string(i));
      if (!ubo.isValid()) {
        return false;
      }
      uniformBuffers_.push_back(std::move(ubo));
    }

    // Create descriptor pool
    vk::DescriptorPoolSize poolSize{vk::DescriptorType::eUniformBuffer,
                                    framesInFlight};

    vk::DescriptorPoolCreateInfo poolInfo{
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, framesInFlight,
        1, &poolSize};

    try {
      descriptorPool_ =
          std::make_unique<vk::raii::DescriptorPool>(device.getRaiiDevice(), poolInfo);
    } catch (const vk::SystemError &) {
      return false;
    }

    // Allocate descriptor sets
    std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                                 descriptorSetLayout);
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

      vk::WriteDescriptorSet descriptorWrite{
          *descriptorSets_[i], 0, 0, 1,
          vk::DescriptorType::eUniformBuffer, nullptr, &bufferInfo, nullptr};

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
    initialized_ = false;
  }

  /**
   * @brief Update uniform buffer data for the current frame
   * @param frameIndex Current frame index
   * @param viewMatrix View matrix (16 floats, column-major)
   * @param projMatrix Projection matrix (16 floats, column-major)
   */
  void updateUniforms(uint32_t frameIndex, const float *viewMatrix,
                      const float *projMatrix) {
    std::lock_guard<std::mutex> lock(objectMutex_);

    if (!initialized_ || frameIndex >= uniformBuffers_.size()) {
      return;
    }

    UniformBufferData uboData{};
    buildModelMatrix(uboData.model);

    if (viewMatrix) {
      std::memcpy(uboData.view, viewMatrix, sizeof(float) * 16);
    } else {
      // Identity matrix
      setIdentity(uboData.view);
    }

    if (projMatrix) {
      std::memcpy(uboData.proj, projMatrix, sizeof(float) * 16);
    } else {
      setIdentity(uboData.proj);
    }

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
  void buildModelMatrix(float *matrix) const {
    setIdentity(matrix);

    if constexpr (Dim == 1) {
      // 1D: Translation along X, scale along X
      matrix[0] = transform_.scale;
      matrix[12] = transform_.position;
    } else if constexpr (Dim == 2) {
      // 2D: Translation XY, rotation around Z, scale XY
      float cosR = std::cos(transform_.rotation);
      float sinR = std::sin(transform_.rotation);

      matrix[0] = transform_.scaleX * cosR;
      matrix[1] = transform_.scaleX * sinR;
      matrix[4] = transform_.scaleY * -sinR;
      matrix[5] = transform_.scaleY * cosR;
      matrix[12] = transform_.positionX;
      matrix[13] = transform_.positionY;
    } else {
      // 3D and higher: Full transform
      float cx = std::cos(transform_.rotationX);
      float sx = std::sin(transform_.rotationX);
      float cy = std::cos(transform_.rotationY);
      float sy = std::sin(transform_.rotationY);
      float cz = std::cos(transform_.rotationZ);
      float sz = std::sin(transform_.rotationZ);

      // Column-major rotation matrix (ZYX order)
      matrix[0] = transform_.scaleX * (cy * cz);
      matrix[1] = transform_.scaleX * (cy * sz);
      matrix[2] = transform_.scaleX * (-sy);

      matrix[4] = transform_.scaleY * (sx * sy * cz - cx * sz);
      matrix[5] = transform_.scaleY * (sx * sy * sz + cx * cz);
      matrix[6] = transform_.scaleY * (sx * cy);

      matrix[8] = transform_.scaleZ * (cx * sy * cz + sx * sz);
      matrix[9] = transform_.scaleZ * (cx * sy * sz - sx * cz);
      matrix[10] = transform_.scaleZ * (cx * cy);

      matrix[12] = transform_.positionX;
      matrix[13] = transform_.positionY;
      matrix[14] = transform_.positionZ;
    }
  }

  std::string name_;
  const MaterialTag *materialTag_ = nullptr;
  TransformType transform_{};

  // Per-frame GPU resources
  std::vector<AllocatedBuffer> uniformBuffers_;
  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;

  bool initialized_ = false;
  mutable std::mutex objectMutex_;
};

// Common type aliases
using Object1D = Object<1>;
using Object2D = Object<2>;
using Object3D = Object<3>;

} // namespace device

#endif // OBJECT_H_
