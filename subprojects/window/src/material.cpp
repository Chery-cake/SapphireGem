#include "material.h"
#include "shader_manager.h"
#include "vulkan/vulkan.hpp"
#include "vulkan_device.h"
#include <mutex>
#include <print>

namespace window {

Material::Material(const MaterialTag &tag)
    : name_(tag.name), shaderTag_(tag.shaderTag), textureTag_(tag.textureTag) {}

Material::~Material() { release(); }

Material::Material(Material &&other) noexcept {
  std::lock_guard<std::mutex> lock(other.materialMutex_);
  name_ = std::move(other.name_);
  shaderTag_ = other.shaderTag_;
  textureTag_ = other.textureTag_;
  other.shaderTag_ = nullptr;
  other.textureTag_ = nullptr;
  shaderProgram_ = other.shaderProgram_;
  other.shaderProgram_ = nullptr;
  initialized_ = other.initialized_;
  other.initialized_ = false;
}

Material &Material::operator=(Material &&other) noexcept {
  if (this != &other) {
    std::scoped_lock lock(materialMutex_, other.materialMutex_);
    release();
    name_ = std::move(other.name_);
    shaderTag_ = other.shaderTag_;
    textureTag_ = other.textureTag_;
    other.shaderTag_ = nullptr;
    other.textureTag_ = nullptr;
    shaderProgram_ = other.shaderProgram_;
    other.shaderProgram_ = nullptr;
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

void Material::release() {
  shaderProgram_ = nullptr;
  initialized_ = false;
}

bool Material::initialize(device::ShaderManager &shaderManager) {
  std::lock_guard<std::mutex> lock(materialMutex_);

  if (initialized_) {
    std::println(stderr, "[Material] Material already initialized: {}", name_);
    return false;
  }

  if (!shaderTag_) {
    std::println(stderr, "[Material] No shader tag set for material: {}",
                 name_);
    return false;
  }

  // Acquire shader program (compiles if needed, increments ref count)
  shaderProgram_ = shaderManager.acquire(shaderTag_);
  if (!shaderProgram_ || !shaderProgram_->compiled) {
    std::println(stderr, "[Material] Failed to acquire shader program for: {}",
                 name_);
    return false;
  }

  auto stageInfos = shaderProgram_->getStageInfos();
  if (stageInfos.empty()) {
    std::println(stderr, "[Material] No valid shader stages for material: {}",
                 name_);
    return false;
  }

  initialized_ = true;
  std::println("[Material] Initialized material: {} ({} shader stages)", name_,
               stageInfos.size());
  return true;
}

ObjectPipeline Material::createPipelineForObject(
    device::GPUDevice &device, vk::RenderPass renderPass,
    vk::DescriptorSetLayout descriptorSetLayout,
    const PipelineConfig &pipelineConfig) {
  std::lock_guard<std::mutex> lock(materialMutex_);

  ObjectPipeline result;

  if (!initialized_ || !shaderProgram_) {
    std::println(stderr,
                 "[Material] Cannot create pipeline: material '{}' not "
                 "initialized",
                 name_);
    return result;
  }

  auto stageInfos = shaderProgram_->getStageInfos();
  if (stageInfos.empty()) {
    std::println(stderr,
                 "[Material] No valid shader stages for pipeline creation: {}",
                 name_);
    return result;
  }

  // Create pipeline layout using the object's descriptor set layout
  if (!createPipelineLayout(device, descriptorSetLayout, result)) {
    std::println(stderr,
                 "[Material] Failed to create pipeline layout for object: {}",
                 name_);
    return result;
  }

  // Create graphics pipeline
  if (!createPipeline(device, renderPass, pipelineConfig, stageInfos, result)) {
    std::println(stderr,
                 "[Material] Failed to create pipeline for object: {}", name_);
    result.reset();
    return result;
  }

  std::println("[Material] Created pipeline for object using material: {}",
               name_);
  return result;
}

bool Material::createPipelineLayout(
    device::GPUDevice &device, vk::DescriptorSetLayout descriptorSetLayout,
    ObjectPipeline &out) {
  vk::PipelineLayoutCreateInfo layoutInfo{{}, 1, &descriptorSetLayout};

  try {
    out.pipelineLayout = std::make_unique<vk::raii::PipelineLayout>(
        device.getRaiiDevice(), layoutInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Material] Failed to create pipeline layout: {}",
                 e.what());
    return false;
  }
}

bool Material::createPipeline(
    device::GPUDevice &device, vk::RenderPass renderPass,
    const PipelineConfig &config,
    const std::vector<vk::PipelineShaderStageCreateInfo> &stages,
    ObjectPipeline &out) {

  // Vertex input state (no vertex buffers for geometry-shader-based
  // rendering)
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

  // Input assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      {}, config.topology, vk::False};

  // Viewport and scissor (dynamic state)
  vk::PipelineViewportStateCreateInfo viewportState{{}, 1, nullptr, 1, nullptr};

  // Rasterization
  vk::PipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.depthClampEnable = vk::False;
  rasterizer.rasterizerDiscardEnable = vk::False;
  rasterizer.polygonMode = config.polygonMode;
  rasterizer.lineWidth = config.lineWidth;
  rasterizer.cullMode = config.cullMode;
  rasterizer.frontFace = config.frontFace;
  rasterizer.depthBiasEnable = vk::False;

  // Multisampling
  vk::PipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sampleShadingEnable = vk::False;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

  // Depth stencil
  vk::PipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.depthTestEnable = config.depthTestEnable ? vk::True : vk::False;
  depthStencil.depthWriteEnable =
      config.depthWriteEnable ? vk::True : vk::False;
  depthStencil.depthCompareOp = vk::CompareOp::eLess;

  // Color blending
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  colorBlendAttachment.blendEnable = config.blendEnable ? vk::True : vk::False;
  if (config.blendEnable) {
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor =
        vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
  }

  vk::PipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.logicOpEnable = vk::False;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &colorBlendAttachment;

  // Dynamic state
  std::array<vk::DynamicState, 2> dynamicStates = {vk::DynamicState::eViewport,
                                                   vk::DynamicState::eScissor};
  vk::PipelineDynamicStateCreateInfo dynamicState{
      {}, static_cast<uint32_t>(dynamicStates.size()), dynamicStates.data()};

  // Create pipeline
  vk::GraphicsPipelineCreateInfo pipelineInfo{};
  pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
  pipelineInfo.pStages = stages.data();
  pipelineInfo.pVertexInputState = &vertexInputInfo;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &rasterizer;
  pipelineInfo.pMultisampleState = &multisampling;
  pipelineInfo.pDepthStencilState = &depthStencil;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.pDynamicState = &dynamicState;
  pipelineInfo.layout = **out.pipelineLayout;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  try {
    out.pipeline = std::make_unique<vk::raii::Pipeline>(device.getRaiiDevice(),
                                                        nullptr, pipelineInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Material] Failed to create graphics pipeline: {}",
                 e.what());
    return false;
  }
}

} // namespace window
