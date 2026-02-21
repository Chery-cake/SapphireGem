#include "glm/ext/matrix_transform.hpp"
#include "object.h"
#include <cstring>
#include <print>

namespace window {

Object::Object(const ObjectTag &tag)
    : name_(tag.name), baseMaterialTag_(tag.baseMaterialTag),
      dimension_(tag.dimension), position_(tag.dimension, 0.0f),
      rotation_(tag.dimension, 0.0f), scale_(tag.dimension, 1.0f) {
  faces_.resize(tag.faceCount);
  for (uint32_t i = 0; i < tag.faceCount; ++i) {
    faces_[i].faceIndex = i;
  }
}

Object::~Object() { release(); }

Object::Object(Object &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.objectMutex_);
  name_ = other.name_;
  other.name_ = nullptr;
  baseMaterialTag_ = other.baseMaterialTag_;
  other.baseMaterialTag_ = nullptr;
  dimension_ = other.dimension_;
  position_ = std::move(other.position_);
  rotation_ = std::move(other.rotation_);
  scale_ = std::move(other.scale_);
  faces_ = std::move(other.faces_);
  objectPipeline_ = std::move(other.objectPipeline_);
  uniformBuffers_ = std::move(other.uniformBuffers_);
  descriptorPool_ = std::move(other.descriptorPool_);
  descriptorSets_ = std::move(other.descriptorSets_);
  descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
  initialized_ = other.initialized_;
  other.initialized_ = false;
}

Object &Object::operator=(Object &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(objectMutex_, other.objectMutex_);
    release();
    name_ = other.name_;
    other.name_ = nullptr;
    baseMaterialTag_ = other.baseMaterialTag_;
    other.baseMaterialTag_ = nullptr;
    dimension_ = other.dimension_;
    position_ = std::move(other.position_);
    rotation_ = std::move(other.rotation_);
    scale_ = std::move(other.scale_);
    faces_ = std::move(other.faces_);
    objectPipeline_ = std::move(other.objectPipeline_);
    uniformBuffers_ = std::move(other.uniformBuffers_);
    descriptorPool_ = std::move(other.descriptorPool_);
    descriptorSets_ = std::move(other.descriptorSets_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

bool Object::initialize(device::VMAAllocator &allocator,
                        device::GPUDevice &device, Material &baseMaterial,
                        vk::RenderPass renderPass, uint32_t framesInFlight,
                        const PipelineConfig &pipelineConfig) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (initialized_) {
    std::println(stderr,
                 "[Object] Already initialized: {} - call release() first",
                 name_);
    return false;
  }

  if (!baseMaterialTag_) {
    std::println(stderr, "[Object] No base material tag set for object: {}",
                 name_);
    return false;
  }

  if (!baseMaterial.isInitialized()) {
    std::println(stderr,
                 "[Object] Base material '{}' not initialized for object: {}",
                 baseMaterial.getName(), name_);
    return false;
  }

  if (framesInFlight == 0) {
    std::println(stderr, "[Object] framesInFlight must be > 0 for object: {}",
                 name_);
    return false;
  }

  // Create descriptor set layout (owned by this object)
  vk::DescriptorSetLayoutBinding uboLayoutBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};

  vk::DescriptorSetLayoutCreateInfo layoutCreateInfo{{}, 1, &uboLayoutBinding};

  try {
    descriptorSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutCreateInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor set layout for '{}': {}",
                 name_, e.what());
    return false;
  }

  // Create per-object pipeline from base material
  objectPipeline_ = baseMaterial.createPipelineForObject(
      device, renderPass, **descriptorSetLayout_, pipelineConfig);
  if (!objectPipeline_.isValid()) {
    std::println(stderr, "[Object] Failed to create pipeline for object: {}",
                 name_);
    descriptorSetLayout_.reset();
    return false;
  }

  // Create uniform buffers (one per frame in flight)
  // UBO size is always sizeof(UniformBufferData) — the model matrix is
  // always 4x4 on the GPU. The object's dimension controls how the CPU
  // computes the model matrix from the transform vectors.
  uniformBuffers_.reserve(framesInFlight);
  for (uint32_t i = 0; i < framesInFlight; ++i) {
    auto ubo = allocator.createUniformBuffer(sizeof(UniformBufferData),
                                             std::string(name_) + "_ubo_" +
                                                 std::to_string(i));
    if (!ubo.isValid()) {
      std::println(stderr,
                   "[Object] Failed to create uniform buffer {} for '{}'", i,
                   name_);
      release();
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
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to create descriptor pool for '{}': {}",
                 name_, e.what());
    release();
    return false;
  }

  // Allocate descriptor sets
  std::vector<vk::DescriptorSetLayout> layouts(framesInFlight,
                                               **descriptorSetLayout_);
  vk::DescriptorSetAllocateInfo allocInfo{**descriptorPool_, framesInFlight,
                                          layouts.data()};

  try {
    auto sets = vk::raii::DescriptorSets(device.getRaiiDevice(), allocInfo);
    descriptorSets_.reserve(sets.size());
    for (auto &s : sets) {
      descriptorSets_.push_back(std::move(s));
    }
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Object] Failed to allocate descriptor sets for '{}': {}",
                 name_, e.what());
    release();
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
  std::println("[Object] Initialized '{}' ({}D, {} faces, {} frames)", name_,
               dimension_, faces_.size(), framesInFlight);
  return true;
}

void Object::release() {
  descriptorSets_.clear();
  descriptorPool_.reset();
  uniformBuffers_.clear();
  objectPipeline_.reset();
  descriptorSetLayout_.reset();
  initialized_ = false;
}

bool Object::setFaceMaterial(uint32_t faceIndex, Material *material) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (faceIndex >= faces_.size()) {
    return false;
  }
  faces_[faceIndex].overrideMaterial = material;
  return true;
}

bool Object::setFaceVertices(uint32_t faceIndex, uint32_t vertexOffset,
                             uint32_t vertexCount) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (faceIndex >= faces_.size()) {
    return false;
  }
  faces_[faceIndex].vertexOffset = vertexOffset;
  faces_[faceIndex].vertexCount = vertexCount;
  return true;
}

void Object::setPosition(const std::vector<float> &pos) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  for (uint32_t i = 0; i < dimension_ && i < pos.size(); ++i) {
    position_[i] = pos[i];
  }
}

void Object::setRotation(const std::vector<float> &rot) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  for (uint32_t i = 0; i < dimension_ && i < rot.size(); ++i) {
    rotation_[i] = rot[i];
  }
}

void Object::setScale(const std::vector<float> &scl) {
  std::lock_guard<std::mutex> lock(objectMutex_);
  for (uint32_t i = 0; i < dimension_ && i < scl.size(); ++i) {
    scale_[i] = scl[i];
  }
}

std::vector<float> Object::getPosition() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return position_;
}

std::vector<float> Object::getRotation() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return rotation_;
}

std::vector<float> Object::getScale() const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  return scale_;
}

const Face *Object::getFace(uint32_t index) const {
  std::lock_guard<std::mutex> lock(objectMutex_);
  if (index >= faces_.size()) {
    return nullptr;
  }
  return &faces_[index];
}

void Object::updateUniforms(uint32_t frameIndex, const glm::mat4 &viewMatrix,
                            const glm::mat4 &projMatrix) {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_) {
    return;
  }

  if (frameIndex >= uniformBuffers_.size()) {
    std::println(stderr, "[Object] Invalid frame index {} (max {}) for '{}'",
                 frameIndex, uniformBuffers_.size() - 1, name_);
    return;
  }

  UniformBufferData uboData{};
  uboData.model = buildModelMatrix();
  uboData.view = viewMatrix;
  uboData.proj = projMatrix;

  void *mapped = uniformBuffers_[frameIndex].map();
  if (mapped) {
    std::memcpy(mapped, &uboData, sizeof(UniformBufferData));
    uniformBuffers_[frameIndex].unmap();
  }
}

void Object::draw(vk::CommandBuffer cmd, uint32_t frameIndex) const {
  std::lock_guard<std::mutex> lock(objectMutex_);

  if (!initialized_) {
    return;
  }

  if (frameIndex >= descriptorSets_.size()) {
    std::println(stderr,
                 "[Object] Invalid frame index {} (max {}) for draw '{}'",
                 frameIndex, descriptorSets_.size() - 1, name_);
    return;
  }

  if (!objectPipeline_.isValid()) {
    std::println(stderr, "[Object] No valid pipeline for draw '{}'", name_);
    return;
  }

  // Bind per-object pipeline
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics,
                   **objectPipeline_.pipeline);

  // Bind per-object descriptor set for this frame
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         **objectPipeline_.pipelineLayout, 0,
                         {*descriptorSets_[frameIndex]}, {});

  // Draw each face
  for (const auto &face : faces_) {
    if (face.vertexCount == 0) {
      continue;
    }
    cmd.draw(face.vertexCount, 1, face.vertexOffset, 0);
  }
}

/**
 * @brief Build a 4x4 model matrix from the dimension-agnostic transform
 *
 * Works for any dimension by mapping position/rotation/scale to glm::mat4.
 * For dimension >= 3, uses full 3D transforms.
 * For dimension == 2, rotation is around the Z axis only.
 * For dimension == 1, only translation along X is applied.
 * For dimension > 3, higher dimensions are ignored in the 4x4 matrix.
 */
glm::mat4 Object::buildModelMatrix() const {
  glm::mat4 model(1.0f);

  // Extract position (up to 3 components)
  glm::vec3 pos(0.0f);
  if (dimension_ >= 1)
    pos.x = position_[0];
  if (dimension_ >= 2)
    pos.y = position_[1];
  if (dimension_ >= 3)
    pos.z = position_[2];

  // Extract scale (up to 3 components)
  glm::vec3 scl(1.0f);
  if (dimension_ >= 1)
    scl.x = scale_[0];
  if (dimension_ >= 2)
    scl.y = scale_[1];
  if (dimension_ >= 3)
    scl.z = scale_[2];

  // Apply transformations: T * Rz * Ry * Rx * S
  model = glm::translate(model, pos);

  // Apply rotations
  if (dimension_ >= 3) {
    // Full 3D rotation: Z, then Y, then X (Euler angles)
    model = glm::rotate(model, rotation_[2], glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::rotate(model, rotation_[1], glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotation_[0], glm::vec3(1.0f, 0.0f, 0.0f));
  } else if (dimension_ == 2) {
    // 2D rotation: first component rotates around Z axis
    model = glm::rotate(model, rotation_[0], glm::vec3(0.0f, 0.0f, 1.0f));
  } else if (dimension_ == 1) {
    // 1D: rotation around Z axis
    model = glm::rotate(model, rotation_[0], glm::vec3(0.0f, 0.0f, 1.0f));
  }

  model = glm::scale(model, scl);

  return model;
}

} // namespace window
