#include "material.h"
#include "buffer.h"
#include "common.h"
#include "config.h"
#include "image.h"
#include "logical_device.h"
#include "shader_compiler.h"
#include "vulkan/vulkan.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
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
      createInfo(createInfo), color(1.0f), rougthness(0.5f), metalic(0),
      logicalDevices(devices) {

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

bool render::Material::create_shader_module(
    device::LogicalDevice *device, const std::vector<char> &code,
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

bool render::Material::create_pipeline(device::LogicalDevice *device,
                                       DeviceMaterialResources &resources,
                                       const MaterialCreateInfo &createInfo) {
  try {
    vk::PipelineShaderStageCreateInfo vertShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = *resources.vertexShader,
        .pName = "vertMain"};

    vk::PipelineShaderStageCreateInfo fragShaderStageInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = *resources.fragmentShader,
        .pName = "fragMain"};

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

  // Helper function to ensure shader is compiled
  auto ensureShaderCompiled = [](const std::string& shaderPath) -> std::string {
    std::filesystem::path path(shaderPath);
    
    // If it's already a .spv file, just return it
    if (path.extension() == ".spv") {
      return shaderPath;
    }
    
    // If it's a .slang file, compile it to .spv
    if (path.extension() == ".slang") {
      std::filesystem::path outputPath = path;
      outputPath.replace_extension(".spv");
      
      // Check if .spv exists and is newer than .slang
      bool needsCompilation = !std::filesystem::exists(outputPath);
      if (!needsCompilation) {
        auto slangTime = std::filesystem::last_write_time(path);
        auto spvTime = std::filesystem::last_write_time(outputPath);
        needsCompilation = (slangTime > spvTime);
      }
      
      if (needsCompilation) {
        std::print("Compiling shader: {} -> {}\n", path.string(), outputPath.string());
        static ShaderCompiler compiler; // Shared compiler instance
        
        if (!compiler.compileShaderToSpirv(path, outputPath)) {
          std::print(stderr, "Failed to compile shader {}: {}\n", 
                    path.string(), compiler.getLastError());
          return ""; // Return empty string on failure
        }
      }
      
      return outputPath.string();
    }
    
    // Unknown shader type, return as-is
    return shaderPath;
  };

  // Compile shaders if needed
  std::string vertexSpvPath = ensureShaderCompiled(createInfo.vertexShaders);
  std::string fragmentSpvPath = ensureShaderCompiled(createInfo.fragmentShaders);
  
  if (vertexSpvPath.empty() || fragmentSpvPath.empty()) {
    std::print(stderr, "Failed to prepare shaders for material: {}\n", identifier);
    return false;
  }

  std::vector<char> vertexCode;
  std::vector<char> fragmentCode;

  if (!general::Common::readFile(vertexSpvPath, vertexCode) ||
      !general::Common::readFile(fragmentSpvPath, fragmentCode)) {
    std::print("Failed to load shader files for material: {}\n", identifier);
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

    device->submit_task([this, device, &resources, vertexCode, fragmentCode,
                         promise]() {
      try {
        // Create shader modules
        if (!create_shader_module(device, vertexCode, resources.vertexShader) ||
            !create_shader_module(device, fragmentCode,
                                  resources.fragmentShader)) {
          promise->set_value(false);
          return;
        }

        // Create pipeline for this device
        promise->set_value(create_pipeline(device, resources, createInfo));
      } catch (const std::exception &e) {
        std::print(
            "Failed to create pipeline for device {}: {}\n",
            device->get_physical_device()->get_properties().deviceName.data(),
            e.what());
        promise->set_value(false);
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

void render::Material::set_roughness(const float &newRougthness) {
  std::lock_guard lock(materialMutex);
  rougthness = newRougthness;
}

void render::Material::set_metallic(const float &newMetallic) {
  std::lock_guard lock(materialMutex);
  metalic = newMetallic;
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
