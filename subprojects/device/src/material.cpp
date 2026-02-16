#include "material.h"
#include <print>

namespace device {

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
  pipeline_ = std::move(other.pipeline_);
  pipelineLayout_ = std::move(other.pipelineLayout_);
  descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
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
    pipeline_ = std::move(other.pipeline_);
    pipelineLayout_ = std::move(other.pipelineLayout_);
    descriptorSetLayout_ = std::move(other.descriptorSetLayout_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

bool Material::initialize(ShaderManager &shaderManager, GPUDevice &device,
                           vk::RenderPass renderPass,
                           const PipelineConfig &pipelineConfig) {
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
  ShaderProgram *program = shaderManager.acquire(shaderTag_);
  if (!program || !program->compiled) {
    std::println(stderr,
                 "[Material] Failed to acquire shader program for: {}", name_);
    return false;
  }

  auto stageInfos = program->getStageInfos();
  if (stageInfos.empty()) {
    std::println(stderr,
                 "[Material] No valid shader stages for material: {}", name_);
    return false;
  }

  // Create descriptor set layout
  if (!createDescriptorSetLayout(device)) {
    std::println(stderr,
                 "[Material] Failed to create descriptor set layout: {}",
                 name_);
    return false;
  }

  // Create pipeline layout
  if (!createPipelineLayout(device)) {
    std::println(stderr, "[Material] Failed to create pipeline layout: {}",
                 name_);
    return false;
  }

  // Create graphics pipeline
  if (!createPipeline(device, renderPass, pipelineConfig, stageInfos)) {
    std::println(stderr, "[Material] Failed to create pipeline: {}", name_);
    return false;
  }

  initialized_ = true;
  std::println("[Material] Initialized material: {}", name_);
  return true;
}

void Material::release() {
  pipeline_.reset();
  pipelineLayout_.reset();
  descriptorSetLayout_.reset();
  initialized_ = false;
}

void Material::bind(vk::CommandBuffer cmd) const {
  std::lock_guard<std::mutex> lock(materialMutex_);
  if (!initialized_ || !pipeline_) {
    return;
  }
  cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);
}

void Material::bindDescriptorSets(
    vk::CommandBuffer cmd,
    const std::vector<vk::DescriptorSet> &descriptorSets) const {
  std::lock_guard<std::mutex> lock(materialMutex_);
  if (!initialized_ || !pipelineLayout_) {
    return;
  }
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **pipelineLayout_,
                         0, descriptorSets, {});
}

vk::Pipeline Material::getPipeline() const {
  std::lock_guard<std::mutex> lock(materialMutex_);
  return pipeline_ ? **pipeline_ : vk::Pipeline{};
}

vk::PipelineLayout Material::getPipelineLayout() const {
  std::lock_guard<std::mutex> lock(materialMutex_);
  return pipelineLayout_ ? **pipelineLayout_ : vk::PipelineLayout{};
}

vk::DescriptorSetLayout Material::getDescriptorSetLayout() const {
  std::lock_guard<std::mutex> lock(materialMutex_);
  return descriptorSetLayout_ ? **descriptorSetLayout_
                              : vk::DescriptorSetLayout{};
}

bool Material::createDescriptorSetLayout(GPUDevice &device) {
  // UBO binding at binding 0
  vk::DescriptorSetLayoutBinding uboBinding{
      0, vk::DescriptorType::eUniformBuffer, 1,
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eGeometry |
          vk::ShaderStageFlagBits::eFragment};

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {uboBinding};

  // If material has a texture, add sampler binding
  if (textureTag_) {
    vk::DescriptorSetLayoutBinding samplerBinding{
        1, vk::DescriptorType::eCombinedImageSampler, 1,
        vk::ShaderStageFlagBits::eFragment};
    bindings.push_back(samplerBinding);
  }

  vk::DescriptorSetLayoutCreateInfo layoutInfo{
      {}, static_cast<uint32_t>(bindings.size()), bindings.data()};

  try {
    descriptorSetLayout_ = std::make_unique<vk::raii::DescriptorSetLayout>(
        device.getRaiiDevice(), layoutInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[Material] Failed to create descriptor set layout: {}",
                 e.what());
    return false;
  }
}

bool Material::createPipelineLayout(GPUDevice &device) {
  vk::DescriptorSetLayout setLayout = **descriptorSetLayout_;
  vk::PipelineLayoutCreateInfo layoutInfo{{}, 1, &setLayout};

  try {
    pipelineLayout_ = std::make_unique<vk::raii::PipelineLayout>(
        device.getRaiiDevice(), layoutInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Material] Failed to create pipeline layout: {}",
                 e.what());
    return false;
  }
}

bool Material::createPipeline(
    GPUDevice &device, vk::RenderPass renderPass,
    const PipelineConfig &config,
    const std::vector<vk::PipelineShaderStageCreateInfo> &stages) {

  // Vertex input state (no vertex buffers for geometry-shader-based rendering)
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo{};

  // Input assembly
  vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
      {}, config.topology, VK_FALSE};

  // Viewport and scissor (dynamic state)
  vk::PipelineViewportStateCreateInfo viewportState{{}, 1, nullptr, 1, nullptr};

  // Rasterization
  vk::PipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = config.polygonMode;
  rasterizer.lineWidth = config.lineWidth;
  rasterizer.cullMode = config.cullMode;
  rasterizer.frontFace = config.frontFace;
  rasterizer.depthBiasEnable = VK_FALSE;

  // Multisampling
  vk::PipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

  // Depth stencil
  vk::PipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
  depthStencil.depthCompareOp = vk::CompareOp::eLess;

  // Color blending
  vk::PipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask =
      vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
      vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
  colorBlendAttachment.blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE;
  if (config.blendEnable) {
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eZero;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
  }

  vk::PipelineColorBlendStateCreateInfo colorBlending{};
  colorBlending.logicOpEnable = VK_FALSE;
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
  pipelineInfo.layout = **pipelineLayout_;
  pipelineInfo.renderPass = renderPass;
  pipelineInfo.subpass = 0;

  try {
    pipeline_ = std::make_unique<vk::raii::Pipeline>(
        device.getRaiiDevice(), nullptr, pipelineInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[Material] Failed to create graphics pipeline: {}",
                 e.what());
    return false;
  }
}

} // namespace device
