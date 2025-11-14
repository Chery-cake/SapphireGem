#include "renderer.h"
#include "config.h"
#include "devices_manager.h"
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
  PFN_vkGetInstanceProcAddr vkInstAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  VULKAN_HPP_DEFAULT_DISPATCHER.init(vkInstAddr);

  init_instance();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
  init_debug();

  init_surface();

  init_device();
  VULKAN_HPP_DEFAULT_DISPATCHER.init(
      *devicesManager->get_primary_device()->get_device());

  init_swap_chain();
}

Renderer::~Renderer() {
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

void Renderer::init_swap_chain() {
  devicesManager->create_swap_chain();
  devicesManager->create_swap_image_views();
}

void Renderer::init_debug() {
  debugMessanger = Config::get_instance().set_up_debug_messanger(instance);
}
