#include "config.h"
#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_raii.hpp>

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::fprintf(stderr, "validation layer: type %u msg: %s\n",
                 static_cast<uint32_t>(type), pCallbackData->pMessage);
  }
  return VK_FALSE;
}

// Wrapper to convert C callback to C++ callback signature
static VKAPI_ATTR vk::Bool32 VKAPI_CALL
debugCallbackCpp(vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
                 vk::DebugUtilsMessageTypeFlagsEXT type,
                 const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
                 void *pUserData) {
  // Call the C version with casted parameters
  return debugCallback(
      static_cast<VkDebugUtilsMessageSeverityFlagBitsEXT>(severity),
      static_cast<VkDebugUtilsMessageTypeFlagsEXT>(type),
      reinterpret_cast<const VkDebugUtilsMessengerCallbackDataEXT *>(
          pCallbackData),
      pUserData);
}

general::Config &general::Config::get_instance() {
  static Config instance;
  return instance;
}

general::Config::Config()
    : instanceLayers({}), instanceExtensions([] {
        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions =
            glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        return std::vector<const char *>(glfwExtensions,
                                         glfwExtensions + glfwExtensionCount);
      }()),
      deviceLayers({}), deviceExtensions({vk::KHRSwapchainExtensionName}),
      optionalInstanceExtensions({}), optionalDeviceExtensions({}),
      maxFramesInFligth(2), reload(false),
      vmaVulkanFunctionsInitialized(false) {
  if (enableValidationLayers) {
    instanceLayers.push_back("VK_LAYER_KHRONOS_validation");
    instanceExtensions.push_back(vk::EXTDebugUtilsExtensionName);
  }
}

general::Config::~Config() { std::print("Config destructor executed\n"); };

vk::raii::DebugUtilsMessengerEXT
general::Config::set_up_debug_messanger(vk::raii::Instance &instance) {
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
      .pfnUserCallback = &debugCallbackCpp};
  return instance.createDebugUtilsMessengerEXT(
      debugUtilsMessengerCreateInfoEXT);
}

void general::Config::initialize_vma_functions() {
  vmaVulkanFunctions.vkGetInstanceProcAddr =
      VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
  vmaVulkanFunctions.vkGetDeviceProcAddr =
      VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;

  vmaVulkanFunctionsInitialized = true;

  std::print("VMA Vulkan functions initialized\n");
}

vk::StructureChain<PhysicalDeviceFeaturesList>
general::Config::get_features(vk::raii::PhysicalDevice physicalDevice) {
  if (*physicalDevice == VK_NULL_HANDLE) {
    vk::PhysicalDeviceFeatures deviceFeatures = {
        .depthClamp = VK_TRUE,
        .samplerAnisotropy = VK_TRUE}; // Enable anisotropic filtering

    vk::StructureChain<PhysicalDeviceFeaturesList> featureChain = {
        {.features = deviceFeatures},  // vk::PhysicalDeviceFeatures2
        {},                            // vk::PhysicalDeviceVulkan11Features
        {.bufferDeviceAddress = true}, // vk::PhysicalDeviceVulkan12Features
        {.synchronization2 = true,
         .dynamicRendering = true}, // vk::PhysicalDeviceVulkan13Features
        {.extendedDynamicState =
             true} // vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT
    };
    return featureChain;
  }
  return physicalDevice.getFeatures2<PhysicalDeviceFeaturesList>();
}

bool general::Config::validate_instance_requirements(
    const vk::raii::Context &context) {
  auto layersPresent = context.enumerateInstanceLayerProperties();

  std::print("Validating instance requirements...\n");
  std::print("Available layers: {}\n", layersPresent.size());

  for (const auto &requiredLayer : instanceLayers) {
    bool found = std::ranges::any_of(
        layersPresent, [&requiredLayer](const auto &available) {
          return strcmp(available.layerName, requiredLayer) == 0;
        });
    if (!found) {
      std::string error = "Instance required layer not supported: " +
                          std::string(requiredLayer);
      std::fprintf(stderr, "ERROR: %s\n", error.c_str());
      throw std::runtime_error(error);
    } else {
      std::print("✓ Layer found: {}\n", requiredLayer);
    }
  }

  auto extensionsPresent = context.enumerateInstanceExtensionProperties();
  std::print("Available extensions: {}\n", extensionsPresent.size());

  for (const auto &requiredExtension : instanceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&requiredExtension](const auto &available) {
          return strcmp(available.extensionName, requiredExtension) == 0;
        });
    if (!found) {
      std::string error = "Instance required extension not supported: " +
                          std::string(requiredExtension);
      std::fprintf(stderr, "ERROR: %s\n", error.c_str());
      throw std::runtime_error(error);
    } else {
      std::print("✓ Extension found: {}\n", requiredExtension);
    }
  }

  return true;
}

bool general::Config::validate_device_requirements(
    const vk::raii::PhysicalDevice &device) {

  auto layersPresent = device.enumerateDeviceLayerProperties();

  std::print("Validating device requirements...\n");
  std::print("Available device layers: {}\n", layersPresent.size());

  for (const auto &requiredLayer : deviceLayers) {
    bool found = std::ranges::any_of(
        layersPresent, [&requiredLayer](const auto &available) {
          return strcmp(available.layerName, requiredLayer) == 0;
        });
    if (!found) {
      std::string error =
          "Device required layer not supported: " + std::string(requiredLayer);
      std::fprintf(stderr, "ERROR: %s\n", error.c_str());
      throw std::runtime_error(error);
    } else {
      std::print("✓ Device layer found: {}\n", requiredLayer);
    }
  }

  auto extensionsPresent = device.enumerateDeviceExtensionProperties();
  std::print("Available device extensions: {}\n", extensionsPresent.size());

  for (const auto &requiredExtension : deviceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&requiredExtension](const auto &available) {
          return strcmp(available.extensionName, requiredExtension) == 0;
        });
    if (!found) {
      std::string error = "Device required extension not supported: " +
                          std::string(requiredExtension);
      std::fprintf(stderr, "ERROR: %s\n", error.c_str());
      throw std::runtime_error(error);
    } else {
      std::print("✓ Device extension found: {}\n", requiredExtension);
    }
  }

  return true;
}

void general::Config::check_and_enable_optional_instance_extensions(
    const vk::raii::Context &context) {
  auto extensionsPresent = context.enumerateInstanceExtensionProperties();

  std::print("Checking optional instance extensions...\n");
  for (const auto &optionalExtension : optionalInstanceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&optionalExtension](const auto &available) {
          return strcmp(available.extensionName, optionalExtension) == 0;
        });
    if (found) {
      std::print("✓ Optional extension available, enabling: {}\n",
                 optionalExtension);
      instanceExtensions.push_back(optionalExtension);
    } else {
      std::print("⚠ Optional extension not available: {}\n", optionalExtension);
    }
  }
  optionalInstanceExtensions.clear();
}

void general::Config::check_and_enable_optional_device_extensions(
    const vk::raii::PhysicalDevice &device) {
  auto extensionsPresent = device.enumerateDeviceExtensionProperties();

  std::print("Checking optional device extensions...\n");
  for (const auto &optionalExtension : optionalDeviceExtensions) {
    bool found = std::ranges::any_of(
        extensionsPresent, [&optionalExtension](const auto &available) {
          return strcmp(available.extensionName, optionalExtension) == 0;
        });
    if (found) {
      std::print("✓ Optional extension available, enabling: {}\n",
                 optionalExtension);
      deviceExtensions.push_back(optionalExtension);
    } else {
      std::print("⚠ Optional extension not available: {}\n", optionalExtension);
    }
  }
  optionalDeviceExtensions.clear();
}

void general::Config::add_instance_layer(const char *layer) {
  instanceLayers.push_back(layer);
  reload = true;
}
void general::Config::remove_instance_layer(const char *layer) {
  auto it = std::find(instanceLayers.begin(), instanceLayers.end(), layer);
  if (it != instanceLayers.end()) {
    instanceLayers.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &general::Config::get_instance_layer() const {
  return instanceLayers;
}

void general::Config::add_instance_extension(const char *extension) {
  instanceExtensions.push_back(extension);
  reload = true;
}
void general::Config::remove_instance_extension(const char *extension) {
  auto it = std::find(instanceExtensions.begin(), instanceExtensions.end(),
                      extension);
  if (it != instanceExtensions.end()) {
    instanceExtensions.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &
general::Config::get_instance_extension() const {
  return instanceExtensions;
}

void general::Config::add_device_layer(const char *layer) {
  deviceLayers.push_back(layer);
  reload = true;
}
void general::Config::remove_device_layer(const char *layer) {
  auto it = std::find(deviceLayers.begin(), deviceLayers.end(), layer);
  if (it != deviceLayers.end()) {
    deviceLayers.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &general::Config::get_device_layer() const {
  return deviceLayers;
}

void general::Config::add_device_extension(const char *extension) {
  deviceExtensions.push_back(extension);
  reload = true;
}
void general::Config::remove_device_extension(const char *extension) {
  auto it =
      std::find(deviceExtensions.begin(), deviceExtensions.end(), extension);
  if (it != deviceExtensions.end()) {
    deviceExtensions.erase(it);
    reload = true;
  }
}
const std::vector<const char *> &general::Config::get_device_extension() const {
  return deviceExtensions;
}

void general::Config::add_optional_instance_extension(const char *extension) {
  optionalInstanceExtensions.push_back(extension);
}

void general::Config::add_optional_device_extension(const char *extension) {
  optionalDeviceExtensions.push_back(extension);
}

void general::Config::set_api_version(uint32_t &version) {
  apiVersion = version;
}
const uint32_t general::Config::get_api_version() const { return apiVersion; }

void general::Config::set_max_frames(uint8_t &max) { maxFramesInFligth = max; }
const uint8_t &general::Config::get_max_frames() const {
  return maxFramesInFligth;
}

const VmaVulkanFunctions *general::Config::get_vma_vulkan_functions() {
  if (vmaVulkanFunctionsInitialized) {
    return &vmaVulkanFunctions;
  }

  initialize_vma_functions();
  return &vmaVulkanFunctions;
}

bool general::Config::needs_reload() const { return reload; }

void general::Config::mark_reload_complete() {
  reload = false;
  initialize_vma_functions();
}
