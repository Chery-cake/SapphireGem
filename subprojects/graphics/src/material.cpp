#include "material.h"
#include "common.h"
#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <array>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <print>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

vk::VertexInputBindingDescription Material::Vertex2D::getBindingDescription() {
  return {0, sizeof(Material::Vertex2D), vk::VertexInputRate::eVertex};
}

std::array<vk::VertexInputAttributeDescription, 2>
Material::Vertex2D::getAttributeDescriptions() {
  return {
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat,
                                          offsetof(Material::Vertex2D, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat,
                                          offsetof(Material::Vertex2D, color))};
}

Material::Material(const std::vector<LogicalDevice *> &devices,
                   const MaterialCreateInfo &createInfo)
    : initialized(false), identifier(createInfo.identifier), color(1.0f),
      rougthness(0.5f), metalic(0), logicalDevices(devices) {

  deviceResources.reserve(devices.size());
  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    deviceResources.push_back(std::make_unique<DeviceMaterialResources>());
  }

  initialize(createInfo);
}

Material::~Material() {

  std::lock_guard lock(materialMutex);

  for (auto &resources : deviceResources) {
    resources.reset();
  }
  deviceResources.clear();

  std::print("Material - {} - destructor executed\n", identifier);
}

bool Material::create_shader_module(LogicalDevice *device,
                                    const std::vector<char> &code,
                                    vk::raii::ShaderModule &shaderModule) {
  try {
    vk::ShaderModuleCreateInfo shaderInfo{
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(code.data())};
    shaderModule = device->get_device().createShaderModule(shaderInfo);
    return true;
  } catch (const std::exception &e) {
    std::print("Failed to create shader module: {}\n", e.what());
    return false;
  }
}

bool Material::create_pipeline(LogicalDevice *device,
                               DeviceMaterialResources &resources,
                               const MaterialCreateInfo &createInfo) {
  try {
    std::string vertName = "vert-" + identifier;
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *resources.vertexShader,
        .pName = vertName.c_str()};

    std::string fragName = "frag-" + identifier;
    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = *resources.fragmentShader,
        .pName = fragName.c_str()};

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = {
        vertShaderStageInfo, fragShaderStageInfo};

    // Create descriptor set layout if needed
    if (!createInfo.descriptorBindings.empty()) {
      vk::DescriptorSetLayoutCreateInfo layoutInfo{
          .bindingCount =
              static_cast<uint32_t>(createInfo.descriptorBindings.size()),
          .pBindings = createInfo.descriptorBindings.data()};
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
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &format.format};

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

bool Material::initialize(const MaterialCreateInfo &createInfo) {
  std::lock_guard lock(materialMutex);

  if (initialized) {
    return true;
  }

  std::vector<char> vertexCode;
  std::vector<char> fragmentCode;

  if (!Common::readFile(createInfo.vertexShaders, vertexCode) ||
      !Common::readFile(createInfo.fragmentShaders, fragmentCode)) {
    std::print("Failed to load shader files for material: {}\n", identifier);
    return false;
  }

  // Initialize pipelines for all devices asynchronously
  std::vector<std::future<bool>> futures;

  for (size_t i = 0; i < logicalDevices.size(); ++i) {
    auto *device = logicalDevices[i];
    auto &resources = *deviceResources[i];

    // Submit pipeline creation as a task to the device's thread

    std::promise<bool> promise;
    futures.push_back(promise.get_future());

    device->submit_task([this, device, &resources, vertexCode, fragmentCode,
                         &createInfo, &promise]() -> bool {
      try {
        // Create shader modules
        if (!create_shader_module(device, vertexCode, resources.vertexShader) ||
            !create_shader_module(device, fragmentCode,
                                  resources.fragmentShader)) {
          return false;
        }

        // Create pipeline for this device
        return create_pipeline(device, resources, createInfo);
      } catch (const std::exception &e) {
        std::print(
            "Failed to create pipeline for device {}: {}\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
        return false;
      }
    });
  }
  // Wait for all pipelines to be created
  bool allSuccess = true;
  for (size_t i = 0; i < futures.size(); ++i) {
    if (!futures[i].get()) {
      std::print("Failed to initialize material {} on device {}\n", identifier,
                 logicalDevices[i]
                     ->get_physical_device()
                     ->get_properties()
                     .deviceName.data());
      allSuccess = false;
    }
  }

  if (allSuccess) {
    initialized = true;
    std::print("Material - {} - initialized successfully on {} devices\n",
               identifier, logicalDevices.size());
  }

  return allSuccess;
}

void Material::bind(vk::raii::CommandBuffer &commandBuffer,
                    uint32_t deviceIndex) {
  if (!initialized || deviceIndex >= deviceResources.size()) {
    return;
  }

  DeviceMaterialResources &resources = *deviceResources[deviceIndex];
  commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics,
                             *resources.pipeline);

  if (resources.descriptorSet != nullptr) {
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                     *resources.pipelineLayout, 0,
                                     {*resources.descriptorSet}, {});
  }
}

void Material::set_color(const glm::vec4 &newColor) {
  std::lock_guard lock(materialMutex);
  color = newColor;
}

void Material::set_roughness(const float &newRougthness) {
  std::lock_guard lock(materialMutex);
  rougthness = newRougthness;
}

void Material::set_metallic(const float &newMetallic) {
  std::lock_guard lock(materialMutex);
  metalic = newMetallic;
}

bool Material::is_initialized() const { return initialized; }

vk::raii::Pipeline &Material::get_pipeline(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->pipeline;
}

vk::raii::PipelineLayout &Material::get_pipeline_layout(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->pipelineLayout;
}

vk::raii::DescriptorSetLayout &
Material::get_descriptor_set_layout(uint32_t deviceIndex) {
  return deviceResources[deviceIndex]->descriptorLayout;
}

const std::string &Material::get_identifier() const { return identifier; }
