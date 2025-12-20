#pragma once

#include "buffer.h"
#include "image.h"
#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <array>
#include <atomic>
#include <glm/ext/vector_float4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

namespace render {

class Material {
public:

  struct MaterialCreateInfo {
    std::string identifier;

    // shaders path
    std::string vertexShaders;
    std::string fragmentShaders;

    // Layout info
    std::vector<vk::DescriptorSetLayoutBinding> descriptorBindings;

    // pipeline states
    vk::PipelineRasterizationStateCreateInfo rasterizationState;
    vk::PipelineDepthStencilStateCreateInfo depthStencilState;
    vk::PipelineColorBlendStateCreateInfo blendState;
    vk::PipelineVertexInputStateCreateInfo vertexInputState;
    vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState;
    vk::PipelineViewportStateCreateInfo viewportState;
    vk::PipelineMultisampleStateCreateInfo multisampleState;
    std::vector<vk::DynamicState> dynamicStates;
  };

  struct DeviceMaterialResources {
    vk::raii::Pipeline pipeline{nullptr};
    vk::raii::PipelineLayout pipelineLayout{nullptr};
    vk::raii::DescriptorSetLayout descriptorLayout{nullptr};
    vk::raii::ShaderModule vertexShader{nullptr};
    vk::raii::ShaderModule fragmentShader{nullptr};
    std::vector<vk::raii::DescriptorSet> descriptorSets;
  };

private:
  mutable std::mutex materialMutex;
  std::atomic<bool> initialized;

  std::string identifier;
  MaterialCreateInfo createInfo;
  std::vector<std::unique_ptr<DeviceMaterialResources>> deviceResources;

  // shared properties
  glm::vec4 color;
  float rougthness;
  float metalic;

  std::vector<device::LogicalDevice *> logicalDevices;

  bool create_shader_module(device::LogicalDevice *device,
                            const std::vector<char> &code,
                            vk::raii::ShaderModule &shaderModule);
  bool create_pipeline(device::LogicalDevice *device,
                       DeviceMaterialResources &resources,
                       const MaterialCreateInfo &createInfo);

public:
  Material(const std::vector<device::LogicalDevice *> &devices,
           const MaterialCreateInfo &createInfo);
  ~Material();

  // Thread-safe initialization
  bool initialize();
  bool reinitialize();

  // Multi-device support
  void bind(vk::raii::CommandBuffer &commandBuffer, uint32_t deviceIndex = 0,
            uint32_t frameIndex = 0);

  // Texture binding for textured materials
  void bind_texture(Image *image, uint32_t binding = 1,
                    uint32_t deviceIndex = 0);

  // Texture binding for a specific frame (per-object texture support)
  void bind_texture_for_frame(Image *image, uint32_t binding,
                              uint32_t deviceIndex, uint32_t frameIndex);

  // Uniform buffer binding for materials
  void bind_uniform_buffer(device::Buffer *buffer, uint32_t binding = 0,
                           uint32_t deviceIndex = 0);

  void set_color(const glm::vec4 &newColor);
  void set_roughness(const float &newRougthness);
  void set_metallic(const float &newMetallic);

  bool is_initialized() const;

  vk::raii::Pipeline &get_pipeline(uint32_t deviceIndex = 0);
  vk::raii::PipelineLayout &get_pipeline_layout(uint32_t deviceIndex = 0);
  vk::raii::DescriptorSetLayout &
  get_descriptor_set_layout(uint32_t deviceIndex = 0);

  const std::string &get_identifier() const;
};

} // namespace render
