#include "renderer.h"
#include "config.h"
#include "devices_manager.h"
#include "material.h"
#include "material_manager.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <memory>
#include <print>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_raii.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

Renderer::Renderer(GLFWwindow *window)
    : window(window), instance(nullptr), surface(nullptr),
      debugMessanger(nullptr), devicesManager(nullptr) {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  init_instance();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

  init_debug();
  init_surface();

  init_device();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      *devicesManager->get_primary_device()->get_device());

  init_swap_chain();
  init_materials();
}

Renderer::~Renderer() {
  materialManager.reset();
  devicesManager.reset();

  std::print("Renderer destructor executed\n");
}

void Renderer::init_instance() {
  vk::ApplicationInfo appInfo;
  appInfo.pApplicationName = "Vulkan Learning";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_4;

  Config::get_instance().validate_instance_requirements(context);

  vk::InstanceCreateInfo createInfo;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledLayerCount =
      static_cast<uint32_t>(Config::get_instance().get_instance_layer().size());
  createInfo.ppEnabledLayerNames =
      Config::get_instance().get_instance_layer().data();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(
      Config::get_instance().get_instance_extension().size());
  createInfo.ppEnabledExtensionNames =
      Config::get_instance().get_instance_extension().data();

  instance = vk::raii::Instance(context, createInfo);
}

void Renderer::init_surface() {
  VkSurfaceKHR rawSurface = nullptr;

  if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create window surface!");
  }

  surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

void Renderer::init_device() {
  devicesManager = std::make_unique<DevicesManager>(window, instance, surface);
  devicesManager->enumerate_physical_devices();
  devicesManager->initialize_devices();
}

void Renderer::init_swap_chain() { devicesManager->create_swap_chains(); }

void Renderer::init_materials() {
  materialManager = std::make_unique<MaterialManager>(devicesManager.get());

  vk::DescriptorSetLayoutBinding bidingInfo = {
      .binding = 0,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .descriptorCount = 1,
      .stageFlags = vk::ShaderStageFlagBits::eVertex};

  vk::PipelineColorBlendAttachmentState colorBlendAttachment{
      .blendEnable = vk::False,
      .colorWriteMask =
          vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA};

  auto bindingDescription = Material::Vertex2D::getBindingDescription();
  auto attributeDescriptions = Material::Vertex2D::getAttributeDescriptions();

  Material::MaterialCreateInfo createInfo{
      .identifier = "Test",
      .vertexShaders = "",
      .fragmentShaders = "",
      .descriptorBindings = {bidingInfo},
      .rasterizationState = {.depthClampEnable = vk::False,
                             .rasterizerDiscardEnable = vk::False,
                             .polygonMode = vk::PolygonMode::eFill,
                             .cullMode = vk::CullModeFlagBits::eBack,
                             .frontFace = vk::FrontFace::eCounterClockwise,
                             .depthBiasEnable = vk::False,
                             .depthBiasSlopeFactor = 1.0f,
                             .lineWidth = 1.0f},
      .depthStencilState = {},
      .blendState = {.logicOpEnable = vk::False,
                     .logicOp = vk::LogicOp::eCopy,
                     .attachmentCount = 1,
                     .pAttachments = &colorBlendAttachment},
      .vertexInputState{.vertexBindingDescriptionCount = 1,
                        .pVertexBindingDescriptions = &bindingDescription,
                        .vertexAttributeDescriptionCount =
                            static_cast<uint32_t>(attributeDescriptions.size()),
                        .pVertexAttributeDescriptions =
                            attributeDescriptions.data()},
      .inputAssemblyState{.topology = vk::PrimitiveTopology::eTriangleList},
      .viewportState{.viewportCount = 1, .scissorCount = 1},
      .multisampleState{.rasterizationSamples = vk::SampleCountFlagBits::e1,
                        .sampleShadingEnable = vk::False},
      .dynamicStates{vk::DynamicState::eViewport, vk::DynamicState::eScissor}};

  materialManager->add_material(createInfo);
}

void Renderer::init_debug() {
  debugMessanger = Config::get_instance().set_up_debug_messanger(instance);
}

DevicesManager &Renderer::get_device_manager() { return *devicesManager; }
