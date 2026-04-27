#ifndef MATERIAL_H_
#define MATERIAL_H_

#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <cstdint>
#include <memory>
#include <mutex>

// Forward declaration
namespace device {
class GPUDevice;
class ShaderManager;
struct ShaderTag;
struct ShaderProgram;
} // namespace device

namespace window {

/**
 * @brief Tag for identifying materials in the resource system
 *
 * A material tag describes which shaders to use.
 * Must have static storage duration when used with ResourceRegistry.
 */
struct WINDOW_API MaterialTag {
  const char *name;
  const device::ShaderTag *shaderTag = nullptr; // Tag for the shader program

  constexpr MaterialTag(const char *n, const device::ShaderTag *shader)
      : name(n), shaderTag(shader) {}
};

/**
 * @brief Pipeline configuration for material rendering
 */
struct WINDOW_API PipelineConfig { // TODO check default config
  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
  vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
  vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
  bool depthTestEnable = true;
  bool depthWriteEnable = true;
  bool blendEnable = false;
  float lineWidth = 1.0f;

  // Push constants (0 = no push constants)
  uint32_t pushConstantSize = 0;
  vk::ShaderStageFlags pushConstantStages = vk::ShaderStageFlagBits::eVertex;
};

/**
 * @brief Per-object pipeline resources created by a material
 *
 * Each Object gets its own pipeline and pipeline layout to avoid
 * descriptor set conflicts when multiple objects share the same material.
 */
struct WINDOW_API ObjectPipeline {
  std::unique_ptr<vk::raii::Pipeline> pipeline;
  std::unique_ptr<vk::raii::PipelineLayout> pipelineLayout;

  [[nodiscard]] bool isValid() const {
    return pipeline != nullptr && pipelineLayout != nullptr;
  }

  void reset() {
    pipeline.reset();
    pipelineLayout.reset();
  }
};

/**
 * @brief Manages shader usage and pipeline creation for rendering
 *
 * A material binds together shaders and textures. It acts as a pipeline
 * factory: each Object requests its own pipeline and pipeline layout from
 * the material, preventing descriptor set conflicts when multiple objects
 * share the same material.
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
class WINDOW_API Material {
public:
  explicit Material(const MaterialTag &tag);
  ~Material();

  // Disable copy, enable move
  Material(const Material &) = delete;
  Material &operator=(const Material &) = delete;
  Material(Material &&) noexcept;
  Material &operator=(Material &&) noexcept;

  /**
   * @brief Initialize the material by compiling shaders
   *
   * This acquires the shader program but does NOT create a pipeline.
   * Each Object creates its own pipeline via createPipelineForObject().
   * @return true if initialization succeeded
   */
  bool initialize(device::ShaderManager &shaderManager);

  /**
   * @brief Create a pipeline and pipeline layout for a specific object
   *
   * Each object gets its own pipeline to avoid descriptor set conflicts.
   * The textureCount parameter determines how many combined image sampler
   * bindings are included in the pipeline layout (starting at binding 1).
   *
   * @param device GPU device for pipeline creation
   * @param renderPass Render pass for pipeline compatibility
   * @param descriptorSetLayout Layout for the object's descriptor sets (set
   * 0)
   * @param pipelineConfig Pipeline configuration
   * @param textureCount Number of texture sampler bindings (0 = no textures)
   * @param bindlessSetLayout Optional bindless descriptor set layout (set 1)
   * @return ObjectPipeline containing the created pipeline resources
   */
  ObjectPipeline
  createPipelineForObject(device::GPUDevice &device, vk::RenderPass renderPass,
                          vk::DescriptorSetLayout descriptorSetLayout,
                          const PipelineConfig &pipelineConfig = {},
                          uint32_t textureCount = 0,
                          vk::DescriptorSetLayout bindlessSetLayout = {});

  /**
   * @brief Release shader references
   */
  void release();

  /**
   * @brief Bind this material for rendering
   * @param cmd Command buffer to record bind commands
   */
  void bind(vk::CommandBuffer cmd) const;

  /**
   * @brief Bind descriptor sets for this material
   * @param cmd Command buffer
   * @param descriptorSets Descriptor sets to bind
   */
  void bindDescriptorSets(
      vk::CommandBuffer cmd,
      const std::vector<vk::DescriptorSet> &descriptorSets) const;

  // Getters
  [[nodiscard]] const std::string &getName() const { return name_; }
  [[nodiscard]] const device::ShaderTag *getShaderTag() const {
    return shaderTag_;
  }
  const device::ShaderProgram *getShaderProgram() const {
    return shaderProgram_.get();
  }
  [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
  friend class PipelineCache;

  static bool
  createPipelineLayout(device::GPUDevice &device,
                       vk::DescriptorSetLayout descriptorSetLayout,
                       const PipelineConfig &config, ObjectPipeline &out,
                       vk::DescriptorSetLayout bindlessSetLayout = {});
  static bool
  createPipeline(device::GPUDevice &device, vk::RenderPass renderPass,
                 const PipelineConfig &config,
                 const std::vector<vk::PipelineShaderStageCreateInfo> &stages,
                 ObjectPipeline &out);

  std::string name_;
  const device::ShaderTag *shaderTag_ = nullptr;

  // Cached shader program (owned by ShaderManager)
  std::shared_ptr<device::ShaderProgram> shaderProgram_;

  bool initialized_ = false;
  mutable std::mutex materialMutex_;
};

} // namespace window

#endif // MATERIAL_H_
