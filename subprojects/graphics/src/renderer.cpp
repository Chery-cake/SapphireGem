#include "renderer.h"
#include "buffer.h"
#include "buffer_manager.h"
#include "config.h"
#include "device_manager.h"
#include "logical_device.h"
#include "material.h"
#include "material_manager.h"
#include "object_manager.h"
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
      debugMessanger(nullptr), deviceManager(nullptr), textureManager(nullptr),
      bufferManager(nullptr), objectManager(nullptr), currentFrame(0),
      frameCount(0), currentSemaphoreIndex(0) {
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
  init_materials();

  create_buffers();

  objectManager = std::make_unique<ObjectManager>(
      deviceManager.get(), materialManager.get(), bufferManager.get());
  objectManager->set_gpu_config(gpuConfig);
}

Renderer::~Renderer() {
  deviceManager->wait_idle();

  objectManager.reset();
  bufferManager.reset();
  textureManager.reset();
  materialManager.reset();
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

void Renderer::init_materials() {
  materialManager = std::make_unique<MaterialManager>(deviceManager.get());
  textureManager = std::make_unique<TextureManager>(deviceManager.get());

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

  const std::vector<Material::Vertex2D> vertices = {
      {{-0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}},
      {{0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},
      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}}};

  Buffer::BufferCreateInfo vertInfo = {.identifier = "vertices",
                                       .type = Buffer::BufferType::VERTEX,
                                       .usage = Buffer::BufferUsage::STATIC,
                                       .size = std::size(vertices) *
                                               sizeof(Material::Vertex2D),
                                       .initialData = vertices.data()};

  bufferManager->create_buffer(vertInfo);

  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  Buffer::BufferCreateInfo indInfo = {.identifier = "indices",
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = Buffer::BufferUsage::STATIC,
                                      .size =
                                          std::size(indices) * sizeof(uint16_t),
                                      .initialData = indices.data()};

  bufferManager->create_buffer(indInfo);

  struct MaterialUniformData {
    glm::vec4 color;
    float roughness;
    float metallic;
    float padding[2]; // Padding for alignment (vec4 alignment)
  };

  // Get the first material (or iterate through all if needed)
  if (!materialManager->get_materials().empty()) {
    Material *material = materialManager->get_materials()[0];

    MaterialUniformData materialData = {
        .color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), // Default white
        .roughness = 0.5f,
        .metallic = 0.0f,
        .padding = {0.0f, 0.0f}};

    Buffer::BufferCreateInfo matInfo = {.identifier = "material-test",
                                        .type = Buffer::BufferType::UNIFORM,
                                        .usage = Buffer::BufferUsage::DYNAMIC,
                                        .size = sizeof(MaterialUniformData),
                                        .elementSize =
                                            sizeof(MaterialUniformData),
                                        .initialData = &materialData};

    bufferManager->create_buffer(matInfo);
  }
}

bool Renderer::acquire_next_image(LogicalDevice *device, uint32_t &imageIndex,
                                  uint32_t &semaphoreIndex) {
  vk::Result acquireResult;
  try {

    // Use per-frame semaphore for image acquisition
    // This semaphore will be signaled when the image is available
    auto result = device->get_swap_chain().acquire_next_image(
        device->get_image_available_semaphore(currentFrame));
    acquireResult = result.result;
    imageIndex = result.value;

    // The semaphoreIndex will be used to select the render finished
    // semaphore For proper synchronization, we use the acquired image index
    // for present semaphores
    semaphoreIndex = imageIndex;
  } catch (const vk::OutOfDateKHRError &) {
    deviceManager->recreate_swap_chain();
    return false;
  }

  if (acquireResult != vk::Result::eSuccess &&
      acquireResult != vk::Result::eSuboptimalKHR) {
    std::print("Failed to acquire swap chain image: {}\n",
               vk::to_string(acquireResult));
    return false;
  }

  return true;
}

void Renderer::present_frame(LogicalDevice *device, uint32_t imageIndex,
                             uint32_t semaphoreIndex) {
  vk::PresentInfoKHR presentInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores =
          &*device->get_render_finished_semaphore(semaphoreIndex),
      .swapchainCount = 1,
      .pSwapchains = &*device->get_swap_chain().get_swap_chain(),
      .pImageIndices = &imageIndex};

  vk::Result presentResult;
  try {
    presentResult = device->get_graphics_queue().presentKHR(presentInfo);
  } catch (const vk::OutOfDateKHRError &) {
    deviceManager->recreate_swap_chain();
    return;
  }

  if (presentResult == vk::Result::eSuboptimalKHR) {
    deviceManager->recreate_swap_chain();
  }
}

void Renderer::draw_frame_single_gpu(LogicalDevice *device) {
  if (!device) {
    return;
  }

  if (!device->wait_for_fence(currentFrame)) {
    return;
  }
  device->reset_fence(currentFrame);

  uint32_t imageIndex, semaphoreIndex;
  if (!acquire_next_image(device, imageIndex, semaphoreIndex)) {
    return;
  }

  device->begin_command_buffer(currentFrame);

  auto &commandBuffers = device->get_command_buffers();
  if (currentFrame >= commandBuffers.size()) {
    std::print(stderr,
               "ERROR: Frame index {} out of range for command buffers "
               "(size: {})\n",
               currentFrame, commandBuffers.size());
    return;
  }
  vk::raii::CommandBuffer &commandBuffer = commandBuffers[currentFrame];

  device->get_swap_chain().transition_image_for_rendering(commandBuffer,
                                                          imageIndex);
  device->get_swap_chain().begin_rendering(commandBuffer, imageIndex);

  objectManager->render_all_objects(commandBuffer, 0, currentFrame);

  device->get_swap_chain().end_rendering(commandBuffer);
  device->get_swap_chain().transition_image_for_present(commandBuffer,
                                                        imageIndex);
  device->end_command_buffer(currentFrame);
  device->submit_command_buffer(currentFrame, semaphoreIndex, true);
  present_frame(device, imageIndex, semaphoreIndex);
}

void Renderer::draw_frame_afr() {
  // AFR: Each GPU renders different frames
  auto allDevices = deviceManager->get_all_logical_devices();
  if (allDevices.empty()) {
    return;
  }

  const size_t renderingDeviceIndex = frameCount % allDevices.size();
  draw_frame_single_gpu(
      const_cast<LogicalDevice *>(allDevices[renderingDeviceIndex]));
}

void Renderer::draw_frame_sfr(uint32_t imageIndex, uint32_t semaphoreIndex) {
  // SFR: Split frame across multiple GPUs
  auto allDevices = deviceManager->get_all_logical_devices();
  if (allDevices.size() < 2) {
    // Fall back to single GPU if not enough devices
    draw_frame_single_gpu(
        const_cast<LogicalDevice *>(deviceManager->get_primary_device()));
    return;
  }

  // For now, implement horizontal split (top/bottom)
  LogicalDevice *primaryDevice =
      const_cast<LogicalDevice *>(deviceManager->get_primary_device());
  vk::Extent2D extent = primaryDevice->get_swap_chain().get_extent2D();

  // Each device renders a portion of the screen
  for (size_t i = 0; i < allDevices.size(); ++i) {
    LogicalDevice *device = const_cast<LogicalDevice *>(allDevices[i]);

    device->begin_command_buffer(currentFrame);
    vk::raii::CommandBuffer &commandBuffer =
        device->get_command_buffers()[currentFrame];

    device->get_swap_chain().transition_image_for_rendering(commandBuffer,
                                                            imageIndex);
    device->get_swap_chain().begin_rendering(commandBuffer, imageIndex);

    // Adjust viewport for this GPU's portion
    float heightPerGPU = static_cast<float>(extent.height) / allDevices.size();
    vk::Viewport viewport{.x = 0.0f,
                          .y = heightPerGPU * i,
                          .width = static_cast<float>(extent.width),
                          .height = heightPerGPU,
                          .minDepth = 0.0f,
                          .maxDepth = 1.0f};
    commandBuffer.setViewport(0, viewport);

    vk::Rect2D scissor{
        .offset = {0, static_cast<int32_t>(heightPerGPU * i)},
        .extent = {extent.width, static_cast<uint32_t>(heightPerGPU)}};
    commandBuffer.setScissor(0, scissor);

    objectManager->render_all_objects(commandBuffer, i, currentFrame);

    device->get_swap_chain().end_rendering(commandBuffer);

    if (i == 0) {
      device->get_swap_chain().transition_image_for_present(commandBuffer,
                                                            imageIndex);
    }

    device->end_command_buffer(currentFrame);
    device->submit_command_buffer(currentFrame, semaphoreIndex, i == 0);
  }

  present_frame(
      const_cast<LogicalDevice *>(deviceManager->get_primary_device()),
      imageIndex, semaphoreIndex);
}

void Renderer::draw_frame_hybrid() {
  // Hybrid: Choose strategy based on workload
  // For now, use AFR as default
  draw_frame_afr();
}

void Renderer::draw_frame_multi_queue_streaming() {
  // Multi-queue with resource streaming
  // This is an advanced feature that would use async compute/transfer queues
  // For now, fall back to AFR
  draw_frame_afr();
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
  init_materials();
  create_buffers();

  std::print("Reload complete!\n");
}

void Renderer::draw_frame() {
  // Dispatch to strategy-specific implementation
  switch (gpuConfig.strategy) {
  case ObjectManager::RenderStrategy::SINGLE_GPU:
    draw_frame_single_gpu(
        const_cast<LogicalDevice *>(deviceManager->get_primary_device()));
    break;
  case ObjectManager::RenderStrategy::AFR:
    draw_frame_afr();
    break;
  case ObjectManager::RenderStrategy::SFR:
    // SFR needs image index
    {
      auto allDevices = deviceManager->get_all_logical_devices();
      if (!allDevices.empty()) {
        LogicalDevice *primaryDevice =
            const_cast<LogicalDevice *>(deviceManager->get_primary_device());
        if (primaryDevice->wait_for_fence(currentFrame)) {
          primaryDevice->reset_fence(currentFrame);
          uint32_t imageIndex, semaphoreIndex;
          if (acquire_next_image(primaryDevice, imageIndex, semaphoreIndex)) {
            draw_frame_sfr(imageIndex, semaphoreIndex);
          }
        }
      }
    }
    break;
  case ObjectManager::RenderStrategy::HYBRID:
    draw_frame_hybrid();
    break;
  case ObjectManager::RenderStrategy::MULTI_QUEUE_STREAMING:
    draw_frame_multi_queue_streaming();
    break;
  default:
    draw_frame_single_gpu(
        const_cast<LogicalDevice *>(deviceManager->get_primary_device()));
    break;
  }

  // Advance frame counters
  const uint32_t maxFrames = Config::get_instance().get_max_frames();
  currentFrame = (currentFrame + 1) % maxFrames;
  frameCount++;
}

void Renderer::set_render_strategy(ObjectManager::RenderStrategy strategy) {
  gpuConfig.strategy = strategy;
  objectManager->set_render_strategy(strategy);
  std::print("Render strategy changed to: {}\n", static_cast<int>(strategy));
}

void Renderer::set_gpu_config(const ObjectManager::MultiGPUConfig &config) {
  gpuConfig = config;
  objectManager->set_gpu_config(config);
}

DeviceManager &Renderer::get_device_manager() { return *deviceManager; }

MaterialManager &Renderer::get_material_manager() { return *materialManager; }

TextureManager &Renderer::get_texture_manager() { return *textureManager; }

BufferManager &Renderer::get_buffer_manager() { return *bufferManager; }

ObjectManager *Renderer::get_object_manager() { return objectManager.get(); }

RenderObject *Renderer::create_triangle_2d(const std::string &identifier,
                                           const glm::vec3 &position,
                                           const glm::vec3 &rotation,
                                           const glm::vec3 &scale) {
  if (!objectManager) {
    std::print("Error: ObjectManager not initialized\n");
    return nullptr;
  }

  // Define a 2D triangle vertices (in NDC space, z=0)
  const std::vector<Material::Vertex2D> vertices = {
      {{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // Top vertex (red)
      {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}}, // Bottom left vertex (green)
      {{0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}   // Bottom right vertex (blue)
  };

  const std::vector<uint16_t> indices = {0, 1, 2};

  RenderObject::ObjectCreateInfo createInfo{
      .identifier = identifier,
      .type = RenderObject::ObjectType::OBJECT_2D,
      .vertices = vertices,
      .indices = indices,
      .materialIdentifier = "Test",
      .position = position,
      .rotation = rotation,
      .scale = scale,
      .visible = true};

  return objectManager->create_object(createInfo);
}

RenderObject *Renderer::create_cube_3d(const std::string &identifier,
                                       const glm::vec3 &position,
                                       const glm::vec3 &rotation,
                                       const glm::vec3 &scale) {
  if (!objectManager) {
    std::print("Error: ObjectManager not initialized\n");
    return nullptr;
  }

  // Define a 3D cube vertices (8 vertices, using 2D positions but arranged
  // for 3D effect) For a simple 3D cube representation in 2D projection
  constexpr float cubeSize = 0.5f;
  constexpr float depthOffset =
      0.2f; // Offset for back face to create 3D effect

  const std::vector<Material::Vertex2D> vertices = {
      // Front face
      {{-cubeSize, -cubeSize}, {1.0f, 0.0f, 0.0f}}, // 0: Bottom-left
      {{cubeSize, -cubeSize}, {0.0f, 1.0f, 0.0f}},  // 1: Bottom-right
      {{cubeSize, cubeSize}, {0.0f, 0.0f, 1.0f}},   // 2: Top-right
      {{-cubeSize, cubeSize}, {1.0f, 1.0f, 0.0f}},  // 3: Top-left

      // Back face (offset for 3D effect)
      {{-cubeSize + depthOffset, -cubeSize + depthOffset},
       {1.0f, 0.0f, 1.0f}}, // 4: Bottom-left
      {{cubeSize + depthOffset, -cubeSize + depthOffset},
       {0.0f, 1.0f, 1.0f}}, // 5: Bottom-right
      {{cubeSize + depthOffset, cubeSize + depthOffset},
       {1.0f, 1.0f, 1.0f}}, // 6: Top-right
      {{-cubeSize + depthOffset, cubeSize + depthOffset},
       {0.5f, 0.5f, 0.5f}} // 7: Top-left
  };

  // Cube indices for triangles (12 triangles, 2 per face, 6 faces)
  const std::vector<uint16_t> indices = {
      // Front face
      0, 1, 2, 2, 3, 0,
      // Right face (connecting front-right to back-right)
      1, 5, 6, 6, 2, 1,
      // Back face
      5, 4, 7, 7, 6, 5,
      // Left face (connecting front-left to back-left)
      4, 0, 3, 3, 7, 4,
      // Top face
      3, 2, 6, 6, 7, 3,
      // Bottom face
      4, 5, 1, 1, 0, 4};

  RenderObject::ObjectCreateInfo createInfo{
      .identifier = identifier,
      .type = RenderObject::ObjectType::OBJECT_3D,
      .vertices = vertices,
      .indices = indices,
      .materialIdentifier = "Test",
      .position = position,
      .rotation = rotation,
      .scale = scale,
      .visible = true};

  return objectManager->create_object(createInfo);
}
