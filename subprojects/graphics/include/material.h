#pragma once

#include "logical_device.h"
#include "shader.h"
#include "vulkan/vulkan.hpp"
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

    // Shader reference
    Shader *shader =
        nullptr; // Use existing Shader object (raw pointer, not owned)

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

    // Additional shader parameters
    std::unordered_map<std::string, float> floatParams;
    std::unordered_map<std::string, glm::vec4> vec4Params;
  };

  struct DeviceMaterialResources {
    vk::raii::Pipeline pipeline{nullptr};
    vk::raii::PipelineLayout pipelineLayout{nullptr};
    vk::raii::DescriptorSetLayout descriptorLayout{nullptr};
  };

private:
  mutable std::mutex materialMutex;
  std::atomic<bool> initialized;

  std::string identifier;
  MaterialCreateInfo createInfo;
  std::vector<std::unique_ptr<DeviceMaterialResources>> deviceResources;

  // Shader reference (not owned by material)
  Shader *shader;

  // shared properties
  glm::vec4 color;
  float roughness;
  float metallic;
  std::unordered_map<std::string, float> floatParams;
  std::unordered_map<std::string, glm::vec4> vec4Params;

  std::vector<device::LogicalDevice *> logicalDevices;

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
            vk::raii::DescriptorSet *descriptorSet = nullptr);

  void set_color(const glm::vec4 &newColor);
  void set_roughness(const float &newRoughness);
  void set_metallic(const float &newMetallic);
  void set_float_param(const std::string &name, float value);
  void set_vec4_param(const std::string &name, const glm::vec4 &value);

  bool is_initialized() const;

  vk::raii::Pipeline &get_pipeline(uint32_t deviceIndex = 0);
  vk::raii::PipelineLayout &get_pipeline_layout(uint32_t deviceIndex = 0);
  vk::raii::DescriptorSetLayout &
  get_descriptor_set_layout(uint32_t deviceIndex = 0);

  const std::string &get_identifier() const;
  Shader *get_shader() const;
};

} // namespace render
