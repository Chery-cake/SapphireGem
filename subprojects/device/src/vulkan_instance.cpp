#include "vulkan_instance.h"
#include "config.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan/vulkan_hpp_macros.hpp"
#include "vulkan/vulkan_raii.hpp"
#include <memory>
#include <print>
#include <stdexcept>
#include <unordered_set>

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

VulkanInstance::VulkanInstance(VulkanInstance &&other) noexcept
    : context_(std::move(other.context_)),
      instance_(std::move(other.instance_)),
      debugMessenger_(std::move(other.debugMessenger_)),
      initialized_(other.initialized_) {
  other.initialized_ = false;
}

VulkanInstance &VulkanInstance::operator=(VulkanInstance &&other) noexcept {
  if (this != &other) {
    shutdown();
    context_ = std::move(other.context_);
    instance_ = std::move(other.instance_);
    debugMessenger_ = std::move(other.debugMessenger_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

bool VulkanInstance::initialize() {
  if (initialized_) {
    std::println(stderr, "[VulkanInstance] Already initialized");
    return false;
  }

#ifdef __APPLE__
  // Point the Vulkan loader at MoltenVK's ICD manifest.
  // The path is relative to the executable — set once before DynamicLoader.
  // This is the only Apple-specific code needed anywhere in the engine.
  if (!std::getenv("VK_ICD_FILENAMES")) {
    // Only set if not already overridden (e.g. by a dev with the Vulkan
    // SDK)
    putenv("VK_ICD_FILENAMES=vulkan/icd.d/MoltenVK_icd.json");
  }
#endif

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

  // Check instance layer support and add supported layers
  std::vector<const char *> enabledLayers;
  if (!core::Config::instance().getVulkanConfig().instanceLayers.empty()) {
    auto unsupportedList = checkLayerSupport(
        core::Config::instance().getVulkanConfig().instanceLayers);
    std::unordered_set<std::string> unsupported(unsupportedList.begin(),
                                                unsupportedList.end());
    if (!unsupported.empty()) {
      std::println(stderr,
                   "[VulkanInstance] Skipping unsupported instance layers:");
      for (const auto &layer : unsupportedList) {
        std::println(stderr, "  - {}", layer);
      }
    }
    auto &layers = core::Config::instance().getVulkanConfig().instanceLayers;
    for (const auto &layer : layers) {
      if (unsupported.find(layer) == unsupported.end()) {
        enabledLayers.push_back(layer.c_str());
      }
    }
  }

  // Check extension support
  auto unsupportedExtensions = checkExtensionSupport(
      core::Config::instance().getVulkanConfig().instanceExtensions);
  if (!unsupportedExtensions.empty()) {
    std::println(stderr, "[VulkanInstance] Unsupported extensions:");
    for (const auto &ext : unsupportedExtensions) {
      std::println(stderr, "  - {}", ext);
    }
    return false;
  }

  // Prepare extension list
  std::vector<const char *> enabledExtensions;
  auto &extensions =
      core::Config::instance().getVulkanConfig().instanceExtensions;
  enabledExtensions.reserve(extensions.size());
  for (const auto &ext : extensions) {
    enabledExtensions.push_back(ext.c_str());
  }

  // Add debug extension if layers are enabled (for debug messenger)
#ifdef ENGINE_DEBUG
  enabledExtensions.push_back(vk::EXTDebugUtilsExtensionName);
#endif

  vk::ApplicationInfo appInfo{
      core::Config::instance().getApplicationConfig().applicationName.c_str(),
      core::Config::instance().getApplicationConfig().applicationVersion,
      core::Config::instance().getVulkanConfig().engineName.c_str(),
      core::Config::instance().getVulkanConfig().engineVersion,
      core::Config::instance().getVulkanConfig().minApiVersion};

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

  // Setup debug messenger (RAII handles cleanup automatically)
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
  // on systems where the Vulkan loader is present but no ICD is installed.
  vk::detail::DynamicLoader dl;
  auto getInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (!getInstanceProcAddr) {
    throw std::runtime_error(
        "[VulkanInstance] vkGetInstanceProcAddr not available");
  }

  auto enumExtensions =
      reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          getInstanceProcAddr(nullptr,
                              "vkEnumerateInstanceExtensionProperties"));
  if (!enumExtensions) {
    throw std::runtime_error(
        "[VulkanInstance] vkEnumerateInstanceExtensionProperties not available");
  }

  uint32_t count = 0;
  VkResult vkResult = enumExtensions(nullptr, &count, nullptr);
  if (vkResult != VK_SUCCESS || count == 0) {
    return {};
  }

  std::vector<VkExtensionProperties> extensions(count);
  vkResult = enumExtensions(nullptr, &count, extensions.data());
  if (vkResult != VK_SUCCESS) {
    return {};
  }

  std::vector<std::string> result;
  result.reserve(count);
  for (const auto &ext : extensions) {
    result.emplace_back(ext.extensionName);
  }
  return result;
}

std::vector<std::string> VulkanInstance::getAvailableLayers() {
  // Use DynamicLoader + C API directly to avoid vk::raii::Context segfaults
  // on systems where the Vulkan loader is present but no ICD is installed.
  vk::detail::DynamicLoader dl;
  auto getInstanceProcAddr =
      dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
  if (!getInstanceProcAddr) {
    throw std::runtime_error(
        "[VulkanInstance] vkGetInstanceProcAddr not available");
  }

  auto enumLayers =
      reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
          getInstanceProcAddr(nullptr,
                              "vkEnumerateInstanceLayerProperties"));
  if (!enumLayers) {
    throw std::runtime_error(
        "[VulkanInstance] vkEnumerateInstanceLayerProperties not available");
  }

  uint32_t count = 0;
  VkResult vkResult = enumLayers(&count, nullptr);
  if (vkResult != VK_SUCCESS || count == 0) {
    return {};
  }

  std::vector<VkLayerProperties> layers(count);
  vkResult = enumLayers(&count, layers.data());
  if (vkResult != VK_SUCCESS) {
    return {};
  }

  std::vector<std::string> result;
  result.reserve(count);
  for (const auto &layer : layers) {
    result.emplace_back(layer.layerName);
  }
  return result;
}

std::vector<std::string> VulkanInstance::checkExtensionSupport(
    const std::vector<std::string> &extensions) {
  auto available = getAvailableExtensions();
  std::vector<std::string> unsupported;

  for (const auto &ext : extensions) {
    if (std::find(available.begin(), available.end(), ext) == available.end()) {
      unsupported.push_back(ext);
    }
  }

  return unsupported;
}

std::vector<std::string>
VulkanInstance::checkLayerSupport(const std::vector<std::string> &layers) {
  auto available = getAvailableLayers();
  std::vector<std::string> unsupported;

  for (const auto &layer : layers) {
    if (std::find(available.begin(), available.end(), layer) ==
        available.end()) {
      unsupported.push_back(layer);
    }
  }

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
