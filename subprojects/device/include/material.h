#ifndef MATERIAL_H_
#define MATERIAL_H_

#include "device_export.h"
#include "shader_manager.h"
#include "texture.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace device {

/**
 * @brief Tag for identifying materials in the resource system
 *
 * A material tag describes which shaders and texture to use.
 * Must have static storage duration when used with ResourceRegistry.
 */
struct DEVICE_API MaterialTag {
  const char *name;
  const ShaderTag *shaderTag = nullptr;   // Tag for the shader program
  const TextureTag *textureTag = nullptr; // Tag for the texture (optional)

  constexpr MaterialTag(const char *n, const ShaderTag *shader,
                        const TextureTag *tex = nullptr)
      : name(n), shaderTag(shader), textureTag(tex) {}
};

/**
 * @brief Pipeline configuration for material rendering
 */
struct DEVICE_API PipelineConfig {
  vk::PrimitiveTopology topology = vk::PrimitiveTopology::eTriangleList;
  vk::PolygonMode polygonMode = vk::PolygonMode::eFill;
  vk::CullModeFlags cullMode = vk::CullModeFlagBits::eBack;
  vk::FrontFace frontFace = vk::FrontFace::eCounterClockwise;
  bool depthTestEnable = true;
  bool depthWriteEnable = true;
  bool blendEnable = false;
  float lineWidth = 1.0f;
};

/**
 * @brief Manages shader usage, texture binding, and pipeline creation
 *
 * A material binds together shaders and textures, creating the Vulkan
 * graphics pipeline needed to render objects. Materials can be shared
 * across multiple objects to save GPU resources.
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
class DEVICE_API Material {
public:
  explicit Material(const MaterialTag &tag);
  ~Material();

  // Disable copy, enable move
  Material(const Material &) = delete;
  Material &operator=(const Material &) = delete;
  Material(Material &&) noexcept;
  Material &operator=(Material &&) noexcept;

  /**
   * @brief Initialize the material by compiling shaders and setting up pipeline
   * @param shaderManager Shader manager for shader compilation
   * @param device GPU device for pipeline creation
   * @param renderPass Render pass for pipeline compatibility
   * @param pipelineConfig Pipeline configuration
   * @return true if initialization succeeded
   */
  bool initialize(ShaderManager &shaderManager, GPUDevice &device,
                  vk::RenderPass renderPass,
                  const PipelineConfig &pipelineConfig = {});

  /**
   * @brief Release all GPU resources
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
  void bindDescriptorSets(vk::CommandBuffer cmd,
                          const std::vector<vk::DescriptorSet> &descriptorSets) const;

  // Getters
  [[nodiscard]] const std::string &getName() const { return name_; }
  [[nodiscard]] const ShaderTag *getShaderTag() const { return shaderTag_; }
  [[nodiscard]] const TextureTag *getTextureTag() const { return textureTag_; }
  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] vk::Pipeline getPipeline() const;
  [[nodiscard]] vk::PipelineLayout getPipelineLayout() const;
  [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const;

private:
  bool createPipelineLayout(GPUDevice &device);
  bool createPipeline(GPUDevice &device, vk::RenderPass renderPass,
                      const PipelineConfig &config,
                      const std::vector<vk::PipelineShaderStageCreateInfo> &stages);
  bool createDescriptorSetLayout(GPUDevice &device);

  std::string name_;
  const ShaderTag *shaderTag_ = nullptr;
  const TextureTag *textureTag_ = nullptr;

  std::unique_ptr<vk::raii::Pipeline> pipeline_;
  std::unique_ptr<vk::raii::PipelineLayout> pipelineLayout_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout_;

  bool initialized_ = false;
  mutable std::mutex materialMutex_;
};

} // namespace device

#endif // MATERIAL_H_
