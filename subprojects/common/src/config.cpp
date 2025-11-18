#include "config.h"
#include "common.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
    vk::DebugUtilsMessageTypeFlagsEXT type,
    const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
  if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError ||
      severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    Common::print(std::cerr, "validation layer: type {0} msg: {1}\n",
                  to_string(type), pCallbackData->pMessage);
    ;
  }

  return vk::False;
}

Config &Config::get_instance() {
  static Config instance;
  return instance;
}

Config::Config()
    : instanceLayers({}), instanceExtensions([] {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions =
            glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        return std::vector<const char *>(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);
      }()),
      deviceLayers({}),
      deviceExtensions({vk::KHRSwapchainExtensionName,
                        vk::KHRSpirv14ExtensionName,
                        vk::KHRSynchronization2ExtensionName,
                        vk::KHRCreateRenderpass2ExtensionName,
                        vk::KHRDynamicRenderingExtensionName}),
      reload(false) {
  if (enableValidationLayers) {
    instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
    instanceExtensions.push_back(vk::EXTDebugUtilsExtensionName);
  }
}

Config::~Config() { Common::print("Config destructor executed\n"); };

vk::raii::DebugUtilsMessengerEXT
Config::set_up_debug_messanger(vk::raii::Instance &instance) {
  if (!enableValidationLayers)
    return nullptr;

  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
  vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT{
      .messageSeverity = severityFlags,
      .messageType = messageTypeFlags,
      .pfnUserCallback = &debugCallback};
  return instance.createDebugUtilsMessengerEXT(
      debugUtilsMessengerCreateInfoEXT);
}

vk::StructureChain<PhysicalDeviceFeaturesList>
Config::get_features(vk::raii::PhysicalDevice physicalDevice) {
  if (physicalDevice == nullptr) {
    vk::StructureChain<PhysicalDeviceFeaturesList> featureChain = {
        {}, // vk::PhysicalDeviceFeatures2
        {}, // vk::PhysicalDeviceVulkan14Features
        {.extendedDynamicState = true},
        {.bufferDeviceAddress = true},
        {.synchronization2 = true, .dynamicRendering = true}};
    return featureChain;
  }
  return physicalDevice.getFeatures2<PhysicalDeviceFeaturesList>();
}

bool Config::validate_instance_requirements(
    const vk::raii::Context &context) const {
  auto layersPresent = context.enumerateInstanceLayerProperties();

  for (const auto &requiredLayer : instanceLayers) {
    bool found = std::ranges::any_of(
        layersPresent, [&requiredLayer](const auto &available) {
          return strcmp(available.layerName, requiredLayer) == 0;
        });
    if (!found) {
      throw std::runtime_error("Instance required layer not supported: " +
                               std::string(requiredLayer));
    }
  }

  auto extensionsPresent = context.enumerateInstanceExtensionProperties();

  for (const auto &requiredExtension : instanceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&requiredExtension](const auto &available) {
          return strcmp(available.extensionName, requiredExtension) == 0;
        });
    if (!found) {
      throw std::runtime_error("Instance required extension not supported:" +
                               std::string(requiredExtension));
    }
  }

  return true;
}

bool Config::validate_device_requirements(
    const vk::raii::PhysicalDevice &device) const {

  auto layersPresent = device.enumerateDeviceLayerProperties();

  for (const auto &requiredLayer : deviceLayers) {
    bool found = std::ranges::any_of(
        layersPresent, [&requiredLayer](const auto &available) {
          return strcmp(available.layerName, requiredLayer) == 0;
        });
    if (!found) {
      throw std::runtime_error("Device required layer not supported: " +
                               std::string(requiredLayer));
    }
  }

  auto extensionsPresent = device.enumerateDeviceExtensionProperties();

  for (const auto &requiredExtension : deviceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&requiredExtension](const auto &available) {
          return strcmp(available.extensionName, requiredExtension) == 0;
        });
    if (!found) {
      throw std::runtime_error("Device required extension not supported:" +
                               std::string(requiredExtension));
    }
  }

  return true;
}

void Config::add_instance_layer(const char *layer) {
  instanceLayers.push_back(layer);
  reload = true;
}
void Config::remove_instance_layer(const char *layer) {
  auto it = std::find(instanceLayers.begin(), instanceLayers.end(), layer);
  if (it != instanceLayers.end()) {
    instanceLayers.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &Config::get_instance_layer() const {
  return instanceLayers;
}

void Config::add_instance_extension(const char *extension) {
  instanceExtensions.push_back(extension);
  reload = true;
}
void Config::remove_instance_extension(const char *extension) {
  auto it = std::find(instanceExtensions.begin(), instanceExtensions.end(),
                      extension);
  if (it != instanceExtensions.end()) {
    instanceExtensions.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &Config::get_instance_extension() const {
  return instanceExtensions;
}

void Config::add_device_layer(const char *layer) {
  deviceLayers.push_back(layer);
  reload = true;
}
void Config::remove_device_layer(const char *layer) {
  auto it = std::find(deviceLayers.begin(), deviceLayers.end(), layer);
  if (it != deviceLayers.end()) {
    deviceLayers.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &Config::get_device_layer() const {
  return deviceLayers;
}

void Config::add_device_extension(const char *extension) {
  deviceExtensions.push_back(extension);
  reload = true;
}
void Config::remove_device_extension(const char *extension) {
  auto it =
      std::find(deviceExtensions.begin(), deviceExtensions.end(), extension);
  if (it != deviceExtensions.end()) {
    deviceExtensions.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &Config::get_device_extension() const {
  return deviceExtensions;
}
