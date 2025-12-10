#include "renderer.h"
#include "buffer.h"
#include "buffer_manager.h"
#include "config.h"
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
      debugMessanger(nullptr), deviceManager(nullptr), bufferManager(nullptr),
      currentFrame(0) {
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
}

Renderer::~Renderer() {
  bufferManager.reset();
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
      .vertexShaders = "slang_vert.spv",
      .fragmentShaders = "slang_frag.spv",
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
                                       .size = std::size(vertices),
                                       .initialData = &vertices};

  bufferManager->create_buffer(vertInfo);

  const std::vector<uint16_t> indices = {0, 1, 2, 2, 3, 0};

  Buffer::BufferCreateInfo indInfo = {.identifier = "indices",
                                      .type = Buffer::BufferType::INDEX,
                                      .usage = Buffer::BufferUsage::STATIC,
                                      .size = std::size(indices),
                                      .initialData = &indices};

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

DeviceManager &Renderer::get_device_manager() { return *deviceManager; }

void Renderer::drawFrame() {
  const uint32_t maxFrames = Config::get_instance().get_max_frames();
  LogicalDevice *device =
      const_cast<LogicalDevice *>(deviceManager->get_primary_device());

  // Wait for the previous frame to finish
  auto result = device->get_device().waitForFences(
      *device->get_in_flight_fence(currentFrame), VK_TRUE, UINT64_MAX);

  if (result != vk::Result::eSuccess) {
    std::print("Failed to wait for fence\n");
    return;
  }

  device->get_device().resetFences(*device->get_in_flight_fence(currentFrame));

  // Acquire an image from the swap chain
  uint32_t imageIndex;
  vk::Result acquireResult;
  try {
    auto result = device->get_swap_chain().get_swap_chain().acquireNextImage(
        UINT64_MAX, *device->get_image_available_semaphore(currentFrame),
        nullptr);
    acquireResult = result.first;
    imageIndex = result.second;
  } catch (const vk::OutOfDateKHRError &) {
    deviceManager->recreate_swap_chain();
    return;
  }

  if (acquireResult != vk::Result::eSuccess &&
      acquireResult != vk::Result::eSuboptimalKHR) {
    std::print("Failed to acquire swap chain image\n");
    return;
  }

  // Get the command buffer
  vk::raii::CommandBuffer &commandBuffer =
      device->get_command_buffers()[currentFrame];

  // Reset and begin recording command buffer
  commandBuffer.reset();
  vk::CommandBufferBeginInfo beginInfo{};
  commandBuffer.begin(beginInfo);

  // Get swap chain extent and format
  vk::Extent2D extent = device->get_swap_chain().get_extent2D();
  vk::SurfaceFormatKHR surfaceFormat =
      device->get_swap_chain().get_surface_format();

  // Begin dynamic rendering
  vk::RenderingAttachmentInfo colorAttachment{
      .imageView = *device->get_swap_chain().get_image_views()[imageIndex],
      .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .loadOp = vk::AttachmentLoadOp::eClear,
      .storeOp = vk::AttachmentStoreOp::eStore,
      .clearValue = vk::ClearValue(vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f))};

  vk::RenderingInfo renderInfo{.renderArea = vk::Rect2D({0, 0}, extent),
                               .layerCount = 1,
                               .colorAttachmentCount = 1,
                               .pColorAttachments = &colorAttachment};

  // Transition image layout to color attachment optimal
  vk::ImageMemoryBarrier barrier{
      .srcAccessMask = vk::AccessFlagBits::eNone,
      .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
      .oldLayout = vk::ImageLayout::eUndefined,
      .newLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .image = device->get_swap_chain().get_images()[imageIndex],
      .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  commandBuffer.pipelineBarrier(
      vk::PipelineStageFlagBits::eTopOfPipe,
      vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, nullptr, nullptr,
      barrier);

  // Begin rendering
  commandBuffer.beginRendering(renderInfo);

  // Set dynamic viewport and scissor
  vk::Viewport viewport{.x = 0.0f,
                        .y = 0.0f,
                        .width = static_cast<float>(extent.width),
                        .height = static_cast<float>(extent.height),
                        .minDepth = 0.0f,
                        .maxDepth = 1.0f};
  commandBuffer.setViewport(0, viewport);

  vk::Rect2D scissor{.offset = {0, 0}, .extent = extent};
  commandBuffer.setScissor(0, scissor);

  // Bind the material (pipeline)
  if (!materialManager->get_materials().empty()) {
    Material *material = materialManager->get_materials()[0];
    material->bind(commandBuffer, 0, currentFrame);

    // Bind vertex buffer
    Buffer *vertexBuffer = bufferManager->get_buffer("vertices");
    if (vertexBuffer) {
      vertexBuffer->bind_vertex(commandBuffer, 0, 0, 0);
    }

    // Bind index buffer
    Buffer *indexBuffer = bufferManager->get_buffer("indices");
    if (indexBuffer) {
      indexBuffer->bind_index(commandBuffer, vk::IndexType::eUint16, 0, 0);
    }

    // Draw the triangle
    commandBuffer.drawIndexed(6, 1, 0, 0, 0);
  }

  // End rendering
  commandBuffer.endRendering();

  // Transition image layout to present
  vk::ImageMemoryBarrier presentBarrier{
      .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
      .dstAccessMask = vk::AccessFlagBits::eNone,
      .oldLayout = vk::ImageLayout::eColorAttachmentOptimal,
      .newLayout = vk::ImageLayout::ePresentSrcKHR,
      .image = device->get_swap_chain().get_images()[imageIndex],
      .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  commandBuffer.pipelineBarrier(
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
      vk::PipelineStageFlagBits::eBottomOfPipe, {}, nullptr, nullptr,
      presentBarrier);

  // End command buffer recording
  commandBuffer.end();

  // Submit command buffer
  vk::PipelineStageFlags waitStages[] = {
      vk::PipelineStageFlagBits::eColorAttachmentOutput};
  vk::SubmitInfo submitInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &*device->get_image_available_semaphore(currentFrame),
      .pWaitDstStageMask = waitStages,
      .commandBufferCount = 1,
      .pCommandBuffers = &*commandBuffer,
      .signalSemaphoreCount = 1,
      .pSignalSemaphores =
          &*device->get_render_finished_semaphore(currentFrame)};

  device->get_graphics_queue().submit(
      submitInfo, *device->get_in_flight_fence(currentFrame));

  // Present the image
  vk::PresentInfoKHR presentInfo{
      .waitSemaphoreCount = 1,
      .pWaitSemaphores =
          &*device->get_render_finished_semaphore(currentFrame),
      .swapchainCount = 1,
      .pSwapchains = &*device->get_swap_chain().get_swap_chain(),
      .pImageIndices = &imageIndex};

  vk::Result presentResult;
  try {
    presentResult = device->get_graphics_queue().presentKHR(presentInfo);
  } catch (const vk::OutOfDateKHRError &) {
    deviceManager->recreate_swap_chain();
    currentFrame = (currentFrame + 1) % maxFrames;
    return;
  }

  if (presentResult == vk::Result::eSuboptimalKHR) {
    deviceManager->recreate_swap_chain();
  }

  currentFrame = (currentFrame + 1) % maxFrames;
}
