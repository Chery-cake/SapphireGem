#include "renderer.h"
#include "buffer_manager.h"
#include "config.h"
#include "device_manager.h"
#include "logical_device.h"
#include "material_manager.h"
#include "object_manager.h"
#include "texture_manager.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <memory>
#include <print>
#include <stdexcept>
#include <utility>
#include <vector>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_raii.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

render::Renderer::Renderer(GLFWwindow *window)
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

  materialManager = std::make_unique<MaterialManager>(deviceManager.get());

  textureManager = std::make_unique<TextureManager>(deviceManager.get());

  bufferManager = std::make_unique<device::BufferManager>(deviceManager.get());

  objectManager = std::make_unique<ObjectManager>(
      deviceManager.get(), materialManager.get(), bufferManager.get(),
      textureManager.get());
  objectManager->set_gpu_config(gpuConfig);
}

render::Renderer::~Renderer() {
  deviceManager->wait_idle();

  objectManager.reset();
  bufferManager.reset();
  textureManager.reset();
  materialManager.reset();
  deviceManager.reset();

  std::print("Renderer destructor executed\n");
}

void render::Renderer::init_instance() {
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

  general::Config::get_instance().set_api_version(appInfo.apiVersion);
  std::print("Requesting Vulkan API version: {}.{}.{}\n",
             VK_API_VERSION_MAJOR(appInfo.apiVersion),
             VK_API_VERSION_MINOR(appInfo.apiVersion),
             VK_API_VERSION_PATCH(appInfo.apiVersion));

  // Validate required extensions and layers
  general::Config::get_instance().validate_instance_requirements(context);
  // Check and enable optional extensions
  general::Config::get_instance().check_and_enable_optional_instance_extensions(
      context);

  vk::InstanceCreateInfo createInfo;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledLayerCount = static_cast<uint32_t>(
      general::Config::get_instance().get_instance_layer().size());
  createInfo.ppEnabledLayerNames =
      general::Config::get_instance().get_instance_layer().data();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(
      general::Config::get_instance().get_instance_extension().size());
  createInfo.ppEnabledExtensionNames =
      general::Config::get_instance().get_instance_extension().data();

  std::print("Creating Vulkan instance with {} layers and {} extensions\n",
             createInfo.enabledLayerCount, createInfo.enabledExtensionCount);

  instance = vk::raii::Instance(context, createInfo);

  std::print("Vulkan instance created successfully\n");
}

void render::Renderer::init_surface() {
  VkSurfaceKHR rawSurface = nullptr;

  if (glfwCreateWindowSurface(*instance, window, nullptr, &rawSurface) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create window surface!");
  }

  surface = vk::raii::SurfaceKHR(instance, rawSurface);
}

void render::Renderer::init_device() {
  deviceManager =
      std::make_unique<device::DeviceManager>(window, instance, surface);
  deviceManager->enumerate_physical_devices();
  deviceManager->initialize_devices();
}

void render::Renderer::init_swap_chain() {
  deviceManager->create_swap_chains();
  deviceManager->create_command_pool();
}

void render::Renderer::init_debug() {
  debugMessanger =
      general::Config::get_instance().set_up_debug_messanger(instance);
}

bool render::Renderer::acquire_next_image(device::LogicalDevice *device,
                                          uint32_t &imageIndex,
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

void render::Renderer::present_frame(device::LogicalDevice *device,
                                     uint32_t imageIndex,
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

void render::Renderer::draw_frame_single_gpu(device::LogicalDevice *device) {
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

void render::Renderer::draw_frame_afr() {
  // AFR: Each GPU renders different frames
  auto allDevices = deviceManager->get_all_logical_devices();
  if (allDevices.empty()) {
    return;
  }

  const size_t renderingDeviceIndex = frameCount % allDevices.size();
  draw_frame_single_gpu(
      const_cast<device::LogicalDevice *>(allDevices[renderingDeviceIndex]));
}

void render::Renderer::draw_frame_sfr(uint32_t imageIndex,
                                      uint32_t semaphoreIndex) {
  // SFR: Split frame across multiple GPUs
  auto allDevices = deviceManager->get_all_logical_devices();
  if (allDevices.size() < 2) {
    // Fall back to single GPU if not enough devices
    draw_frame_single_gpu(const_cast<device::LogicalDevice *>(
        deviceManager->get_primary_device()));
    return;
  }

  // For now, implement horizontal split (top/bottom)
  device::LogicalDevice *primaryDevice =
      const_cast<device::LogicalDevice *>(deviceManager->get_primary_device());
  vk::Extent2D extent = primaryDevice->get_swap_chain().get_extent2D();

  // Each device renders a portion of the screen
  for (size_t i = 0; i < allDevices.size(); ++i) {
    device::LogicalDevice *device =
        const_cast<device::LogicalDevice *>(allDevices[i]);

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
      const_cast<device::LogicalDevice *>(deviceManager->get_primary_device()),
      imageIndex, semaphoreIndex);
}

void render::Renderer::draw_frame_hybrid() {
  // Hybrid: Choose strategy based on workload
  // For now, use AFR as default
  draw_frame_afr();
}

void render::Renderer::draw_frame_multi_queue_streaming() {
  // Multi-queue with resource streaming
  // This is an advanced feature that would use async compute/transfer queues
  // For now, fall back to AFR
  draw_frame_afr();
}

void render::Renderer::reload() {
  std::print("Reloading rendering system...\n");

  // Call pre-reload callback to allow cleanup of objects that reference managers
  // This prevents use-after-free when scenes/objects hold pointers to managers
  if (preReloadCallback) {
    try {
      preReloadCallback();
    } catch (const std::exception &e) {
      std::print("Warning: Exception in pre-reload callback: {}\n", e.what());
    } catch (...) {
      std::print("Warning: Unknown exception in pre-reload callback\n");
    }
  }

  // Wait for all devices to be idle
  if (deviceManager) {
    deviceManager->wait_idle();
  }

  // Cleanup in reverse order
  objectManager.reset();
  bufferManager.reset();
  textureManager.reset();
  materialManager.reset();
  deviceManager.reset();

  // Check if we need to reload instance (layers/extensions changed)
  if (general::Config::get_instance().needs_reload()) {
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

    general::Config::get_instance().mark_reload_complete();
  } else {
    std::print("Partial reload - keeping instance...\n");
    // Just reload devices if instance doesn't need recreation
  }

  // Recreate device manager and downstream resources
  init_device();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      *deviceManager->get_primary_device()->get_device());

  init_swap_chain();

  materialManager = std::make_unique<MaterialManager>(deviceManager.get());

  textureManager = std::make_unique<TextureManager>(deviceManager.get());

  bufferManager = std::make_unique<device::BufferManager>(deviceManager.get());

  objectManager = std::make_unique<ObjectManager>(
      deviceManager.get(), materialManager.get(), bufferManager.get(),
      textureManager.get());
  objectManager->set_gpu_config(gpuConfig);

  std::print("Reload complete!\n");

  // Notify callback that reload is complete and objects can be recreated
  if (postReloadCallback) {
    try {
      postReloadCallback();
    } catch (const std::exception &e) {
      std::print("Warning: Exception in post-reload callback: {}\n", e.what());
    } catch (...) {
      std::print("Warning: Unknown exception in post-reload callback\n");
    }
  }
}

void render::Renderer::draw_frame() {
  // Dispatch to strategy-specific implementation
  switch (gpuConfig.strategy) {
  case ObjectManager::RenderStrategy::SINGLE_GPU:
    draw_frame_single_gpu(const_cast<device::LogicalDevice *>(
        deviceManager->get_primary_device()));
    break;
  case ObjectManager::RenderStrategy::AFR:
    draw_frame_afr();
    break;
  case ObjectManager::RenderStrategy::SFR:
    // SFR needs image index
    {
      auto allDevices = deviceManager->get_all_logical_devices();
      if (!allDevices.empty()) {
        device::LogicalDevice *primaryDevice =
            const_cast<device::LogicalDevice *>(
                deviceManager->get_primary_device());
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
    draw_frame_single_gpu(const_cast<device::LogicalDevice *>(
        deviceManager->get_primary_device()));
    break;
  }

  // Advance frame counters
  const uint32_t maxFrames = general::Config::get_instance().get_max_frames();
  currentFrame = (currentFrame + 1) % maxFrames;
  frameCount++;
}

void render::Renderer::set_render_strategy(
    ObjectManager::RenderStrategy strategy) {
  gpuConfig.strategy = strategy;
  objectManager->set_render_strategy(strategy);
  std::print("Render strategy changed to: {}\n", static_cast<int>(strategy));
}

void render::Renderer::set_gpu_config(
    const ObjectManager::MultiGPUConfig &config) {
  gpuConfig = config;
  objectManager->set_gpu_config(config);
}

void render::Renderer::set_pre_reload_callback(
    std::function<void()> callback) {
  preReloadCallback = std::move(callback);
}

void render::Renderer::set_post_reload_callback(
    std::function<void()> callback) {
  postReloadCallback = std::move(callback);
}

device::DeviceManager &render::Renderer::get_device_manager() {
  return *deviceManager;
}

render::MaterialManager &render::Renderer::get_material_manager() {
  return *materialManager;
}

render::TextureManager &render::Renderer::get_texture_manager() {
  return *textureManager;
}

device::BufferManager &render::Renderer::get_buffer_manager() {
  return *bufferManager;
}

render::ObjectManager *render::Renderer::get_object_manager() {
  return objectManager.get();
}
