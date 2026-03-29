#include "vulkan_instance.h"
#include "config.h"
#include "config_vulkan.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan_hpp_macros.hpp"
#include "vulkan/vulkan_raii.hpp"
#include <algorithm>
#include <iterator>
#include <memory>
#include <print>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

namespace device {

// Debug callback for validation layers
static VKAPI_ATTR vk::Bool32 VKAPI_CALL
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              vk::DebugUtilsMessageTypeFlagsEXT messageType,
              const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
  (void)pUserData;
  (void)messageType;

  const char *severity = "UNKNOWN";
  if (messageSeverity & vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
    severity = "VERBOSE";
  } else if (messageSeverity &
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
    severity = "INFO";
  } else if (messageSeverity &
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    severity = "WARNING";
  } else if (messageSeverity &
             vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
    severity = "ERROR";
  }

  std::println(stderr, "[Vulkan {}] {}", severity, pCallbackData->pMessage);
  return VK_FALSE;
}

VulkanInstance::VulkanInstance() {};

VulkanInstance::~VulkanInstance() { shutdown(); }

void VulkanInstance::shutdown() {
  if (!initialized_) {
    return;
  }

  debugMessenger_.reset();
  instance_.reset();
  context_.reset();

  initialized_ = false;
  std::println("[VulkanInstance] Shutdown complete");
}

bool VulkanInstance::initialize() {
  if (initialized_) {
    std::println(stderr, "[VulkanInstance] Already initialized");
    return false;
  }

  vk::detail::DynamicLoader dl;
  auto instanceProc =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (!instanceProc) {
    std::println(stderr,
                 "[VulkanInstance] Failed to get vkGetInstanceProcAddr");
    return false;
  }
  VULKAN_HPP_DEFAULT_DISPATCHER.init(instanceProc);

  try {
    context_ = std::make_unique<vk::raii::Context>();
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[VulkanInstance] Failed to create context: {}",
                 e.what());
    return false;
  }

  // Check instance layer support
  auto &layers = core::Config::instance().getVulkanConfig().getInstanceLayers();

  auto unsupportedList = checkLayerSupport(layers);
  std::unordered_set<std::string> unsupported(unsupportedList.begin(),
                                              unsupportedList.end());
  if (!unsupported.empty()) {
    std::println(stderr,
                 "[VulkanInstance] Skipping unsupported instance layers:");
    std::ranges::for_each(
        unsupportedList.begin(), unsupportedList.end(),
        [](const auto &layer) { std::println(stderr, "  - {}", layer); });
    return false;
  }

  auto &optionalLayers =
      core::Config::instance().getVulkanConfig().getOptionalInstanceLayers();

  unsupportedList = checkLayerSupport(optionalLayers);
  unsupported.insert(unsupportedList.begin(), unsupportedList.end());

  if (!unsupported.empty()) {
    std::println(stderr, "[VulkanInstance] Skipping unsupported "
                         "optional instance layers:");
    std::ranges::for_each(
        unsupportedList.begin(), unsupportedList.end(),
        [](const auto &layer) { std::println(stderr, "  - {}", layer); });
  }

  // Add supported instance layers
  std::vector<const char *> enabledLayers;

  std::ranges::copy(layers |
                        std::views::transform([](const std::string &layer) {
                          return layer.c_str();
                        }),
                    std::back_inserter(enabledLayers));

  std::ranges::copy(
      optionalLayers | std::views::filter([&unsupported](const auto &layer) {
        return unsupported.find(layer) == unsupported.end();
      }) | std::views::transform([](const std::string &layer) {
        return layer.c_str();
      }),
      std::back_inserter(enabledLayers));

  // Check extension support
  auto &exts =
      core::Config::instance().getVulkanConfig().getInstanceExtensions();
  auto unsupportedExtensionsList = checkExtensionSupport(exts);
  std::unordered_set<std::string> unsupportedExt(
      unsupportedExtensionsList.begin(), unsupportedExtensionsList.end());
  if (!unsupportedExt.empty()) {
    std::println(stderr, "[VulkanInstance] Unsupported extensions:");
    std::ranges::for_each(
        unsupportedExt.begin(), unsupportedExt.end(),
        [](const auto &ext) { std::println(stderr, "  - {}", ext); });
    return false;
  }

  auto &optionalExts = core::Config::instance()
                           .getVulkanConfig()
                           .getOptionalInstanceExtensions();
  unsupportedExtensionsList = checkExtensionSupport(optionalExts);
  unsupportedExt.insert(unsupportedExtensionsList.begin(),
                        unsupportedExtensionsList.end());

  if (!unsupportedExt.empty()) {
    std::println(stderr, "[VulkanInstance] Unsupported optional extensions:");
    std::ranges::for_each(
        unsupportedExt.begin(), unsupportedExt.end(),
        [](const auto &ext) { std::println(stderr, "  - {}", ext); });
  }

  // Prepare extension list
  std::vector<const char *> enabledExtensions;

  std::ranges::copy(exts | std::views::transform([](const std::string &ext) {
                      return ext.c_str();
                    }),
                    std::back_inserter(enabledExtensions));

  std::ranges::copy(
      optionalExts | std::views::filter([&unsupportedExt](const auto &ext) {
        return unsupportedExt.find(ext) == unsupportedExt.end();
      }) | std::views::transform([](const std::string &ext) {
        return ext.c_str();
      }),
      std::back_inserter(enabledExtensions));

  // Add debug extension if layers are enabled (for debug messenger)
#ifdef ENGINE_DEBUG
  enabledExtensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

  vk::ApplicationInfo appInfo{
      core::Config::instance().getApplicationConfig().applicationName.c_str(),
      core::Config::instance().getApplicationConfig().applicationVersion,
      core::Config::instance().getVulkanConfig().getEngineName().c_str(),
      core::Config::instance().getVulkanConfig().getEngineVersion(),
      core::Config::instance().getVulkanConfig().getMinApiVersion()};

  vk::InstanceCreateInfo createInfo{
      {}, &appInfo, enabledLayers, enabledExtensions};

#ifdef ENGINE_DEBUG
  vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  debugCreateInfo.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
  debugCreateInfo.messageType =
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
  debugCreateInfo.pfnUserCallback = debugCallback;

  createInfo.pNext = &debugCreateInfo;
#endif

  try {
    instance_ = std::make_unique<vk::raii::Instance>(*context_, createInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[VulkanInstance] Failed to create instance: {}",
                 e.what());
    return false;
  }

  VULKAN_HPP_DEFAULT_DISPATCHER.init(**instance_, instanceProc);

  // Setup debug messenger
#ifdef ENGINE_DEBUG
  if (!setupDebugMessenger()) {
    std::println(stderr, "[VulkanInstance] Failed to setup debug messenger");
    // Continue anyway, not critical
  }
#endif

  initialized_ = true;
  std::println("[VulkanInstance] Initialized successfully");
  return true;
}

std::vector<std::string> VulkanInstance::getAvailableExtensions() {
  // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
  vk::detail::DynamicLoader dl;
  auto getInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (getInstanceProcAddr == nullptr) {
    throw std::runtime_error(
        "[VulkanInstance] vkGetInstanceProcAddr not available");
  }

  vk::detail::DispatchLoaderDynamic dld{getInstanceProcAddr};
  auto exts = vk::enumerateInstanceExtensionProperties(nullptr, dld);

  return exts | std::views::transform([](const vk::ExtensionProperties &ext) {
           return std::string(ext.extensionName);
         }) |
         std::ranges::to<std::vector<std::string>>();
}

std::vector<std::string> VulkanInstance::getAvailableLayers() {
  // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
  vk::detail::DynamicLoader dl;
  auto getInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (getInstanceProcAddr == nullptr) {
    throw std::runtime_error(
        "[VulkanInstance] vkGetInstanceProcAddr not available");
  }

  vk::detail::DispatchLoaderDynamic dld{getInstanceProcAddr};
  auto layers = vk::enumerateInstanceLayerProperties(dld);

  return layers | std::views::transform([](const vk::LayerProperties &layer) {
           return std::string(layer.layerName);
         }) |
         std::ranges::to<std::vector<std::string>>();
}

std::vector<std::string> VulkanInstance::checkExtensionSupport(
    const std::vector<std::string> &extensions) {
  auto available = getAvailableExtensions();
  std::unordered_set<std::string> available_set{available.begin(),
                                                available.end()};

  std::vector<std::string> unsupported;

  std::ranges::copy_if(extensions, std::back_inserter(unsupported),
                       [&available_set](const auto &ext) {
                         return !available_set.contains(ext);
                       });

  return unsupported;
}

std::vector<std::string>
VulkanInstance::checkLayerSupport(const std::vector<std::string> &layers) {
  auto available = getAvailableLayers();
  std::unordered_set<std::string> available_set{available.begin(),
                                                available.end()};

  std::vector<std::string> unsupported;

  std::ranges::copy_if(layers, std::back_inserter(unsupported),
                       [&available_set](const auto &layer) {
                         return !available_set.contains(layer);
                       });

  return unsupported;
}

bool VulkanInstance::setupDebugMessenger() {
  if (!instance_) {
    return false;
  }

  vk::DebugUtilsMessengerCreateInfoEXT createInfo{
      {},
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
          vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
      vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
          vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
          vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
      debugCallback,
      nullptr};

  try {
    debugMessenger_ = std::make_unique<vk::raii::DebugUtilsMessengerEXT>(
        *instance_, createInfo);
    return true;
  } catch (const vk::SystemError &e) {
    std::println(stderr,
                 "[VulkanInstance] Failed to create debug messenger: {}",
                 e.what());
    return false;
  }
}

} // namespace device
