#include "config_vulkan.h"
#include "config.h"
#include "vulkan/vulkan.hpp"
#include <algorithm>
#include <iterator>
#include <mutex>

namespace core {

VulkanConfig::VulkanConfig(ConfigSection &pendingChanges, bool &immediateMode,
                           std::vector<Config::CallbackEntry> &callbacks,
                           std::mutex &configMutex)
    : pendingChanges(pendingChanges), immediateMode(immediateMode),
      callbacks(callbacks), configMutex(configMutex) {

  // Initialize with sensible defaults
#ifdef ENGINE_DEBUG
  // Add validation layer in debug builds
  instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif

  // Common instance extensions
  instanceExtensions.emplace_back(vk::KHRSurfaceExtensionName);
#ifdef _WIN32
  instanceExtensions.emplace_back(vk::KHRWin32SurfaceExtensionName);
#elif defined(__linux__)
  instanceExtensions.emplace_back(vk::KHRXcbSurfaceExtensionName);
  instanceExtensions.emplace_back(vk::KHRWaylandSurfaceExtensionName);
#elif defined(__APPLE__)
  instanceExtensions.emplace_back(vk::EXTMetalSurfaceExtensionName);
#endif

  // Common device extensions
  deviceExtensions.emplace_back(vk::KHRSwapchainExtensionName);
}

VulkanConfig::~VulkanConfig() {}

void VulkanConfig::resetToDefaults() {
  std::lock_guard<std::mutex> lock(configVulkanMutex_);

  instanceLayers.clear();
  deviceExtensions.clear();
  instanceLayers.clear();
  optionalInstanceExtensions.clear();
  optionalDeviceExtensions.clear();
  optionalInstanceLayers.clear();

  // Initialize with sensible defaults
#ifdef ENGINE_DEBUG
  // Add validation layer in debug builds
  instanceLayers.emplace_back("VK_LAYER_KHRONOS_validation");
#endif

  // Common instance extensions
  instanceExtensions.emplace_back(vk::KHRSurfaceExtensionName);
#ifdef _WIN32
  instanceExtensions.emplace_back(vk::KHRWin32SurfaceExtensionName);
#elif defined(__linux__)
  instanceExtensions.emplace_back(vk::KHRXcbSurfaceExtensionName);
  instanceExtensions.emplace_back(vk::KHRWaylandSurfaceExtensionName);
#elif defined(__APPLE__)
  instanceExtensions.emplace_back(vk::EXTMetalSurfaceExtensionName);
#endif

  // Common device extensions
  deviceExtensions.emplace_back(vk::KHRSwapchainExtensionName);
}

void VulkanConfig::addInstanceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(instanceExtensions.begin(),
                                instanceExtensions.end(), extension);
    if (it == instanceExtensions.end()) {
      instanceExtensions.push_back(extension);
      changed = true;
      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

void VulkanConfig::addDeviceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(deviceExtensions.begin(),
                                deviceExtensions.end(), extension);
    if (it == deviceExtensions.end()) {
      deviceExtensions.push_back(extension);
      changed = true;
      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

void VulkanConfig::addInstanceLayer(const std::string &layer) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it =
        std::ranges::find(instanceLayers.begin(), instanceLayers.end(), layer);
    if (it == instanceLayers.end()) {
      instanceLayers.push_back(layer);
      changed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

bool VulkanConfig::removeInstanceExtension(const std::string &extension) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(instanceExtensions.begin(),
                                instanceExtensions.end(), extension);
    if (it != instanceExtensions.end()) {
      instanceExtensions.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

bool VulkanConfig::removeDeviceExtension(const std::string &extension) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(deviceExtensions.begin(),
                                deviceExtensions.end(), extension);
    if (it != deviceExtensions.end()) {
      deviceExtensions.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

bool VulkanConfig::removeInstanceLayer(const std::string &layer) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it =
        std::ranges::find(instanceLayers.begin(), instanceLayers.end(), layer);
    if (it != instanceLayers.end()) {
      instanceLayers.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

void VulkanConfig::addOptionalInstanceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalInstanceExtensions.begin(),
                                optionalInstanceExtensions.end(), extension);
    if (it == optionalInstanceExtensions.end()) {
      optionalInstanceExtensions.push_back(extension);
      changed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

void VulkanConfig::addOptionalDeviceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalDeviceExtensions.begin(),
                                optionalDeviceExtensions.end(), extension);
    if (it == optionalDeviceExtensions.end()) {
      optionalDeviceExtensions.push_back(extension);
      changed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

void VulkanConfig::addOptionalInstanceLayer(const std::string &layer) {
  bool changed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalInstanceLayers.begin(),
                                optionalInstanceLayers.end(), layer);
    if (it == optionalInstanceLayers.end()) {
      optionalInstanceLayers.push_back(layer);
      changed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }
}

bool VulkanConfig::removeOptionalInstanceExtension(
    const std::string &extension) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalInstanceExtensions.begin(),
                                optionalInstanceExtensions.end(), extension);
    if (it != optionalInstanceExtensions.end()) {
      optionalInstanceExtensions.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

bool VulkanConfig::removeOptionalDeviceExtension(const std::string &extension) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalDeviceExtensions.begin(),
                                optionalDeviceExtensions.end(), extension);
    if (it != optionalDeviceExtensions.end()) {
      optionalDeviceExtensions.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

bool VulkanConfig::removeOptionalInstanceLayer(const std::string &layer) {
  bool removed = false;
  std::vector<Config::CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configVulkanMutex_);
    auto it = std::ranges::find(optionalInstanceLayers.begin(),
                                optionalInstanceLayers.end(), layer);
    if (it != optionalInstanceLayers.end()) {
      optionalInstanceLayers.erase(it);
      removed = true;

      if (immediateMode) {
        std::ranges::copy_if(callbacks, std::back_inserter(callbacksToNotify),
                             [](const auto &entry) {
                               return hasFlag(entry.sections,
                                              ConfigSection::Vulkan);
                             });
      } else {
        std::lock_guard<std::mutex> lock(configMutex);
        pendingChanges = pendingChanges | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode) {
    std::ranges::for_each(callbacksToNotify,
                          [](const auto &entry) { entry.callback(); });
  }

  return removed;
}

} // namespace core
