#include "renderer.h"
#include "buffer.h"
#include "buffer_manager.h"
#include "config.h"
#include "descriptor_pool_manager.h"
#include "device_manager.h"
#include "material.h"
#include "material_manager.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <iterator>
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
      debugMessanger(nullptr), deviceManager(nullptr),
      descriptorPoolManager(nullptr), bufferManager(nullptr) {
  PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

  init_instance();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

  init_debug();
  init_surface();

  init_device();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      *deviceManager->get_primary_device()->get_device());

  init_swap_chain();
  init_descriptor_pools();
  init_materials();

  create_buffers();
}

Renderer::~Renderer() {
  bufferManager.reset();
  materialManager.reset();
  descriptorPoolManager.reset();
  deviceManager.reset();

  std::print("Renderer destructor executed\n");
}

void Renderer::init_instance() {
  vk::ApplicationInfo appInfo = {.pApplicationName = "Vulkan Engine",
                                 .applicationVersion = VK_MAKE_VERSION(0, 0, 0),
                                 .pEngineName = "No Engine",
                                 .engineVersion = VK_MAKE_VERSION(0, 0, 0)};

  // Query available Vulkan API version
  uint32_t instanceVersion = context.enumerateInstanceVersion();
  std::print("Available Vulkan instance version: {}.{}.{}\n",
             VK_API_VERSION_MAJOR(instanceVersion),
             VK_API_VERSION_MINOR(instanceVersion),
             VK_API_VERSION_PATCH(instanceVersion));

  // Request Vulkan 1.3 if available, otherwise use what's available
  appInfo.apiVersion = (instanceVersion >= VK_API_VERSION_1_3)
                           ? VK_API_VERSION_1_3
                           : instanceVersion;

  Config::get_instance().set_api_version(appInfo.apiVersion);
  std::print("Requesting Vulkan API version: {}.{}.{}\n",
             VK_API_VERSION_MAJOR(appInfo.apiVersion),
             VK_API_VERSION_MINOR(appInfo.apiVersion),
             VK_API_VERSION_PATCH(appInfo.apiVersion));

  // Validate required extensions and layers
  Config::get_instance().validate_instance_requirements(context);
  // Check and enable optional extensions
  Config::get_instance().check_and_enable_optional_instance_extensions(context);

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

  std::print("Creating Vulkan instance with {} layers and {} extensions\n",
             createInfo.enabledLayerCount, createInfo.enabledExtensionCount);

  instance = vk::raii::Instance(context, createInfo);

  std::print("Vulkan instance created successfully\n");
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
  deviceManager = std::make_unique<DeviceManager>(window, instance, surface);
  deviceManager->enumerate_physical_devices();
  deviceManager->initialize_devices();
}

void Renderer::init_swap_chain() {
  deviceManager->create_swap_chains();
  deviceManager->create_command_pool();
}

void Renderer::init_descriptor_pools() {
  std::print("Initializing descriptor pools...\n");
  
  // Get max frames in flight from config
  uint32_t maxFrames = Config::get_instance().get_max_frames();
  
  descriptorPoolManager =
      std::make_unique<DescriptorPoolManager>(deviceManager.get(), maxFrames);
  
  // Define the descriptor pool sizes based on expected usage
  DescriptorPoolManager::DescriptorPoolSizes poolSizes{
      .uniformBufferCount = 10,  // Support for multiple uniform buffers
      .storageBufferCount = 5,
      .combinedImageSamplerCount = 10,
      .storageImageCount = 0,
      .inputAttachmentCount = 0};
  
  if (!descriptorPoolManager->initialize(poolSizes)) {
    throw std::runtime_error("Failed to initialize descriptor pools");
  }
  
  std::print("✓ Descriptor pools initialized\n");
}

void Renderer::init_materials() {
  materialManager = std::make_unique<MaterialManager>(deviceManager.get());

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
      .vertexShaders = "slang.spv",
      .fragmentShaders = "slang.spv",
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

void Renderer::create_buffers() {
  bufferManager = std::make_unique<BufferManager>(deviceManager.get());

  std::print("Creating example buffers for triangle rendering...\n");

  // Create vertex buffer with triangle vertices
  const std::vector<Material::Vertex2D> vertices = {
      {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // Bottom-left (red)
      {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},  // Bottom-right (green)
      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},   // Top-right (blue)
      {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}}; // Top-left (white)

  Buffer::BufferCreateInfo vertInfo = {.identifier = "triangle-vertices",
                                       .type = Buffer::BufferType::VERTEX,
                                       .usage = Buffer::BufferUsage::STATIC,
                                       .size = std::size(vertices),
                                       .initialData = vertices.data()};

  bufferManager->create_buffer(vertInfo);
  std::print("✓ Created vertex buffer with {} vertices\n", vertices.size());

  // Create index buffer for drawing two triangles (a quad)
  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  Buffer::BufferCreateInfo indInfo = {.identifier = "triangle-indices",
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = Buffer::BufferUsage::STATIC,
                                      .size = std::size(indices),
                                      .initialData = indices.data()};

  bufferManager->create_buffer(indInfo);
  std::print("✓ Created index buffer with {} indices\n", indices.size());

  // Material uniform buffer structure
  struct MaterialUniformData {
    glm::vec4 color;
    float roughness;
    float metallic;
    float padding[2]; // Padding for alignment (vec4 alignment)
  };

  // Get the first material and allocate descriptor sets for it
  if (!materialManager->get_materials().empty()) {
    Material *material = materialManager->get_materials()[0];

    // Create uniform buffer for material properties
    MaterialUniformData materialData = {
        .color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f), // Magenta for testing
        .roughness = 0.5f,
        .metallic = 0.0f,
        .padding = {0.0f, 0.0f}};

    Buffer::BufferCreateInfo matInfo = {
        .identifier = "material-uniform-test",
        .type = Buffer::BufferType::UNIFORM,
        .usage = Buffer::BufferUsage::DYNAMIC,
        .size = sizeof(MaterialUniformData),
        .elementSize = sizeof(MaterialUniformData),
        .initialData = &materialData};

    Buffer *uniformBuffer = bufferManager->create_buffer(matInfo);
    std::print("✓ Created material uniform buffer\n");

    // Allocate descriptor sets for all devices and frames
    auto logicalDevices = deviceManager->get_all_logical_devices();
    uint32_t maxFrames = descriptorPoolManager->get_max_frames_in_flight();

    for (size_t deviceIndex = 0; deviceIndex < logicalDevices.size();
         ++deviceIndex) {
      // Allocate descriptor sets (one per frame in flight)
      auto &pool = descriptorPoolManager->get_pool(deviceIndex, 0);
      material->allocate_descriptor_sets(pool, maxFrames, deviceIndex);

      // Update each descriptor set to bind the uniform buffer
      for (uint32_t frameIndex = 0; frameIndex < maxFrames; ++frameIndex) {
        VkBuffer buffer = uniformBuffer->get_buffer(deviceIndex);
        material->update_descriptor_set(
            0, buffer, 0, sizeof(MaterialUniformData), frameIndex, deviceIndex);
      }

      std::print("✓ Allocated and updated {} descriptor set(s) for device {}\n",
                 maxFrames, deviceIndex);
    }

    std::print("✓ Material descriptor sets configured\n");
  }

  std::print("✓ All example buffers created successfully\n");
}


void Renderer::reload() {
  std::print("Reloading rendering system...\n");

  // Wait for all devices to be idle
  if (deviceManager) {
    deviceManager->wait_idle();
  }

  // Cleanup in reverse order
  bufferManager.reset();
  materialManager.reset();
  descriptorPoolManager.reset();
  deviceManager.reset();

  // Check if we need to reload instance (layers/extensions changed)
  if (Config::get_instance().needs_reload()) {
    std::print("Full reload required - recreating instance...\n");

    // Clean up everything
    debugMessanger = nullptr;
    surface = nullptr;
    instance = nullptr;

    // Reinitialize everything
    init_instance();
    VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

    init_debug();
    init_surface();

    Config::get_instance().mark_reload_complete();
  } else {
    std::print("Partial reload - keeping instance...\n");
    // Just reload devices if instance doesn't need recreation
  }

  // Recreate device manager and downstream resources
  init_device();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      *deviceManager->get_primary_device()->get_device());

  init_swap_chain();
  init_descriptor_pools();
  init_materials();
  create_buffers();

  std::print("Reload complete!\n");
}

DeviceManager &Renderer::get_device_manager() { return *deviceManager; }
