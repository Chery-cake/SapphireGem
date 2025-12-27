#include "material.h"
#include "buffer.h"
#include "common.h"
#include "config.h"
#include "image.h"
#include "logical_device.h"
#include "slang_wasm_compiler.h"
#include "vulkan/vulkan.hpp"
#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

render::Material::Material(const std::vector<device::LogicalDevice *> &devices,
                           const MaterialCreateInfo &createInfo)
    : initialized(false), identifier(createInfo.identifier),
      createInfo(createInfo), shader(createInfo.shader), color(1.0f),
      roughness(0.5f), metallic(0), floatParams(createInfo.floatParams),
      vec4Params(createInfo.vec4Params), logicalDevices(devices) {

  deviceResources.reserve(logicalDevices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<DeviceMaterialResources>());
  }

  initialize();
}

render::Material::~Material() {

  std::lock_guard lock(materialMutex);

  for (auto &resources : deviceResources) {
    resources.reset();
  }
  deviceResources.clear();

  std::print("Material - {} - destructor executed\n", identifier);
}

bool render::Material::create_pipeline(device::LogicalDevice *device,
                                       DeviceMaterialResources &resources,
                                       const MaterialCreateInfo &createInfo) {
  try {
    // Get shader stage infos from the Shader object
    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages;
    
    if (shader) {
      // Find device index
      uint32_t deviceIndex = 0;
      for (size_t i = 0; i < logicalDevices.size(); ++i) {
        if (logicalDevices[i] == device) {
          deviceIndex = static_cast<uint32_t>(i);
          break;
        }
      }
      shaderStages = shader->get_pipeline_stage_infos(deviceIndex);
    } else {
      std::print(stderr, "Material - {} - no shader provided\n", identifier);
      return false;
    }

    // Create descriptor set layout if needed
    if (!createInfo.descriptorBindings.empty()) {
      vk::DescriptorSetLayoutCreateInfo layoutInfo{
          .bindingCount =
              static_cast<uint32_t>(createInfo.descriptorBindings.size()),
          .pBindings = createInfo.descriptorBindings.data()};
      resources.descriptorLayout =
          device->get_device().createDescriptorSetLayout(layoutInfo);
    } else {
      // Create empty descriptor layout if no bindings
      vk::DescriptorSetLayoutCreateInfo layoutInfo{};
      resources.descriptorLayout =
          device->get_device().createDescriptorSetLayout(layoutInfo);
    }

    // Create pipeline layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo{
        .setLayoutCount = 1,
        .pSetLayouts = &*resources.descriptorLayout,
        .pushConstantRangeCount = 0};

    resources.pipelineLayout =
        device->get_device().createPipelineLayout(pipelineLayoutInfo);

    // Dynamic state
    vk::PipelineDynamicStateCreateInfo dynamicStateInfo{
        .dynamicStateCount =
            static_cast<uint32_t>(createInfo.dynamicStates.size()),
        .pDynamicStates = createInfo.dynamicStates.data()};

    // Pipeline rendering info for dynamic rendering
    vk::SurfaceFormatKHR format = device->get_swap_chain().get_surface_format();

    vk::PipelineRenderingCreateInfo pipelineRenderingInfo{
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &format.format,
        .depthAttachmentFormat = vk::Format::eD32Sfloat};

    // Graphics pipeline creation
    vk::GraphicsPipelineCreateInfo pipelineCreateInfo{
        .pNext = &pipelineRenderingInfo,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &createInfo.vertexInputState,
        .pInputAssemblyState = &createInfo.inputAssemblyState,
        .pViewportState = &createInfo.viewportState,
        .pRasterizationState = &createInfo.rasterizationState,
        .pMultisampleState = &createInfo.multisampleState,
        .pDepthStencilState = &createInfo.depthStencilState,
        .pColorBlendState = &createInfo.blendState,
        .pDynamicState = &dynamicStateInfo,
        .layout = *resources.pipelineLayout,
        .renderPass = VK_NULL_HANDLE, // Using dynamic rendering
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE};

    resources.pipeline = device->get_device().createGraphicsPipeline(
        nullptr, pipelineCreateInfo);

    return true;
  } catch (const std::exception &e) {
    std::print(
        "Failed to create pipeline for device {}: {}\n",
        device->get_physical_device()->get_properties().deviceName.data(),
        e.what());
    return false;
  }
}

bool render::Material::initialize() {
  std::lock_guard lock(materialMutex);

  if (initialized) {
    return true;
  }

  // Shader must be provided and initialized
  if (!shader) {
    std::print(stderr, "Material - {} - no shader provided\n", identifier);
    return false;
  }

  // Initialize pipelines for all devices asynchronously
  std::vector<std::future<bool>> futures;

  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    auto *device = logicalDevices[i];
    auto &resources = *deviceResources[i];

    // Submit pipeline creation as a task to the device's thread
    auto promise = std::make_shared<std::promise<bool>>();
    futures.push_back(promise->get_future());

    device->submit_task([this, device, &resources, promise]() {
      try {
        // Create pipeline for this device
        promise->set_value(create_pipeline(device, resources, createInfo));
      } catch (const std::exception &e) {
        std::print(stderr, "Material - {} - pipeline creation failed: {}\n",
                   identifier, e.what());
        promise->set_value(false);
      }
    });
  }

  // Wait for all pipeline creations to complete
  bool allSuccess = true;
  for (auto &future : futures) {
    if (!future.get()) {
      allSuccess = false;
    }
  }

  if (!allSuccess) {
    std::print(stderr, "Material - {} - failed to initialize\n", identifier);
    return false;
  }

  initialized = true;
  std::print("Material - {} - initialized successfully\n", identifier);
  return true;
}

bool render::Material::reinitialize() {
  {
    std::lock_guard lock(materialMutex);

    initialized = false;
    deviceResources.clear();

    deviceResources.reserve(logicalDevices.size());
    for (size_t i = 0; i < logicalDevices.size(); ++i) {
      deviceResources.push_back(std::make_unique<DeviceMaterialResources>());
    }
  }
  return initialize();
}

void render::Material::bind(vk::raii::CommandBuffer &commandBuffer,
                            uint32_t deviceIndex,
                            vk::raii::DescriptorSet *descriptorSet) {
  if (!initialized) {
    std::print("Warning: Cannot bind uninitialized material '{}'\n",
               identifier);
    return;
  }

  if (deviceIndex >= deviceResources.size()) {
    std::print("Warning: Invalid device index {} for material '{}'\n",
               deviceIndex, identifier);
    return;
  }

  DeviceMaterialResources &resources = *deviceResources[deviceIndex];
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             *resources.pipeline);

  // Bind descriptor set if provided (now managed by Object)
  if (descriptorSet) {
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *resources.pipelineLayout, 0,
                                     {**descriptorSet}, {});
  }
}

void render::Material::set_color(const glm::vec4 &newColor) {
  std::lock_guard lock(materialMutex);
  color = newColor;
}

void render::Material::set_roughness(const float &newRoughness) {
  std::lock_guard lock(materialMutex);
  roughness = newRoughness;
}

void render::Material::set_metallic(const float &newMetallic) {
  std::lock_guard lock(materialMutex);
  metallic = newMetallic;
}

void render::Material::set_float_param(const std::string &name, float value) {
  std::lock_guard lock(materialMutex);
  floatParams[name] = value;
}

void render::Material::set_vec4_param(const std::string &name,
                                      const glm::vec4 &value) {
  std::lock_guard lock(materialMutex);
  vec4Params[name] = value;
}

bool render::Material::is_initialized() const { return initialized; }

vk::raii::Pipeline &render::Material::get_pipeline(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->pipeline;
}

vk::raii::PipelineLayout &
render::Material::get_pipeline_layout(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->pipelineLayout;
}

vk::raii::DescriptorSetLayout &
render::Material::get_descriptor_set_layout(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->descriptorLayout;
}

const std::string &render::Material::get_identifier() const {
  return identifier;
}

render::Shader *render::Material::get_shader() const {
  return shader;
}
