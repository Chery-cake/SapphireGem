#include "config.h"
#include "vulkan/vulkan.hpp"
#include <thread>

namespace core {

#ifdef ENGINE_DEBUG
static Config *g_configInstance = nullptr;
#endif

Config &Config::instance() {
#ifdef ENGINE_DEBUG
  if (g_configInstance) {
    return *g_configInstance;
  }
#endif
  static Config instance;
#ifdef ENGINE_DEBUG
  g_configInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void Config::setInstance(Config *inst) { g_configInstance = inst; }

Config *Config::getInstance() { return g_configInstance; }
#endif

Config::Config() {
  // Initialize with sensible defaults
#ifdef ENGINE_DEBUG
  // Validation layers only in debug builds
  vulkanConfig_.enableValidation = true;

  // Default validation layers (debug only)
  vulkanConfig_.validationLayers.push_back("VK_LAYER_KHRONOS_validation");
#else
  // Validation disabled in release builds
  vulkanConfig_.enableValidation = false;
#endif

  // Common instance extensions
  vulkanConfig_.instanceExtensions.push_back(vk::KHRSurfaceExtensionName);
#ifdef _WIN32
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(__linux__)
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_xcb_surface");
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_wayland_surface");
#elif defined(__APPLE__)
  vulkanConfig_.instanceExtensions.push_back("VK_EXT_metal_surface");
#endif

  // Common device extensions
  vulkanConfig_.deviceExtensions.push_back(vk::KHRSwapchainExtensionName);

  // Auto-detect thread count
  threadPoolAllocation_.workerThreads =
      0; // Will be calculated based on hardware
  threadPoolAllocation_.loopThreads = 1;
  threadPoolAllocation_.gpuThreads = 0;

  // Default GPU config
  gpuConfig_.gpuCount = 1;
  gpuConfig_.preferredGPUIndex = 0;
  gpuConfig_.enableMultiGPU = false;

  // Default loop config
  loopConfig_.mainLoopCount = 1;
  loopConfig_.targetFrameRate = 60;
  loopConfig_.enableVSync = true;
}

Config::~Config() { shutdown(); }

void Config::shutdown() {
  std::lock_guard<std::mutex> lock(configMutex_);
  callbacks_.clear();
  pendingChanges_ = ConfigSection::None;
}

void Config::resetToDefaults() {
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);

    vulkanConfig_ = VulkanConfig{};
#ifdef ENGINE_DEBUG
    // Validation layers only in debug builds
    vulkanConfig_.enableValidation = true;
    vulkanConfig_.validationLayers.push_back("VK_LAYER_KHRONOS_validation");
#else
    // Validation disabled in release builds
    vulkanConfig_.enableValidation = false;
#endif
    vulkanConfig_.instanceExtensions.push_back(vk::KHRSurfaceExtensionName);

    // Add platform-specific instance extensions
#ifdef _WIN32
    vulkanConfig_.instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(__linux__)
    vulkanConfig_.instanceExtensions.push_back("VK_KHR_xcb_surface");
    vulkanConfig_.instanceExtensions.push_back("VK_KHR_wayland_surface");
#elif defined(__APPLE__)
    vulkanConfig_.instanceExtensions.push_back("VK_EXT_metal_surface");
#endif

    vulkanConfig_.deviceExtensions.push_back(vk::KHRSwapchainExtensionName);

    threadPoolAllocation_ = ThreadPoolAllocation{};
    gpuConfig_ = GPUConfig{};
    loopConfig_ = LoopConfig{};

    pendingChanges_ = ConfigSection::All;

    if (immediateMode_) {
      for (const auto &entry : callbacks_) {
        if (hasFlag(pendingChanges_, entry.sections)) {
          callbacksToNotify.push_back(entry);
        }
      }
      pendingChanges_ = ConfigSection::None;
    }
  }

  // Notify callbacks outside lock
  for (const auto &entry : callbacksToNotify) {
    entry.callback();
  }
}

// ========== Vulkan Configuration ==========

void Config::setVulkanConfig(const VulkanConfig &config) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (vulkanConfig_ != config) {
      vulkanConfig_ = config;
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  // Notify callbacks outside lock
  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

VulkanConfig Config::getVulkanConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return vulkanConfig_;
}

void Config::addInstanceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.instanceExtensions.begin(),
                        vulkanConfig_.instanceExtensions.end(), extension);
    if (it == vulkanConfig_.instanceExtensions.end()) {
      vulkanConfig_.instanceExtensions.push_back(extension);
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

void Config::addDeviceExtension(const std::string &extension) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.deviceExtensions.begin(),
                        vulkanConfig_.deviceExtensions.end(), extension);
    if (it == vulkanConfig_.deviceExtensions.end()) {
      vulkanConfig_.deviceExtensions.push_back(extension);
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

void Config::addValidationLayer(const std::string &layer) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.validationLayers.begin(),
                        vulkanConfig_.validationLayers.end(), layer);
    if (it == vulkanConfig_.validationLayers.end()) {
      vulkanConfig_.validationLayers.push_back(layer);
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

bool Config::removeInstanceExtension(const std::string &extension) {
  bool removed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.instanceExtensions.begin(),
                        vulkanConfig_.instanceExtensions.end(), extension);
    if (it != vulkanConfig_.instanceExtensions.end()) {
      vulkanConfig_.instanceExtensions.erase(it);
      removed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }

  return removed;
}

bool Config::removeDeviceExtension(const std::string &extension) {
  bool removed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.deviceExtensions.begin(),
                        vulkanConfig_.deviceExtensions.end(), extension);
    if (it != vulkanConfig_.deviceExtensions.end()) {
      vulkanConfig_.deviceExtensions.erase(it);
      removed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }

  return removed;
}

bool Config::removeValidationLayer(const std::string &layer) {
  bool removed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(vulkanConfig_.validationLayers.begin(),
                        vulkanConfig_.validationLayers.end(), layer);
    if (it != vulkanConfig_.validationLayers.end()) {
      vulkanConfig_.validationLayers.erase(it);
      removed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (removed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }

  return removed;
}

void Config::setValidationEnabled(bool enable) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (vulkanConfig_.enableValidation != enable) {
      vulkanConfig_.enableValidation = enable;
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Vulkan)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Vulkan;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

// ========== Thread Pool Configuration ==========

void Config::setThreadPoolAllocation(const ThreadPoolAllocation &allocation) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (threadPoolAllocation_ != allocation) {
      threadPoolAllocation_ = allocation;
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::ThreadPool)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::ThreadPool;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

ThreadPoolAllocation Config::getThreadPoolAllocation() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return threadPoolAllocation_;
}

ThreadPoolAllocation Config::getEffectiveThreadAllocation() const {
  std::lock_guard<std::mutex> lock(configMutex_);

  ThreadPoolAllocation effective = threadPoolAllocation_;

  // Calculate total threads for loops and GPUs
  // Note: loopThreads and gpuThreads are "per loop" and "per GPU" values
  uint32_t totalLoopThreads = effective.loopThreads * loopConfig_.mainLoopCount;
  uint32_t totalGPUThreads = gpuConfig_.enableMultiGPU
                                 ? (effective.gpuThreads * gpuConfig_.gpuCount)
                                 : effective.gpuThreads;

  // Calculate effective worker threads if set to auto-detect
  if (effective.workerThreads == 0) {
    uint32_t hardwareThreads =
        std::max(1u, std::thread::hardware_concurrency());

    // Reserve threads for loops and GPUs
    uint32_t totalReserved = totalLoopThreads + totalGPUThreads;

    // Ensure we have at least 1 worker thread
    effective.workerThreads = (hardwareThreads > totalReserved)
                                  ? (hardwareThreads - totalReserved)
                                  : 1;
  }

  // Set the effective values (total threads, not per-loop/per-GPU)
  effective.loopThreads = totalLoopThreads;
  effective.gpuThreads = totalGPUThreads;

  return effective;
}

// ========== GPU Configuration ==========

void Config::setGPUConfig(const GPUConfig &config) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (gpuConfig_ != config) {
      gpuConfig_ = config;
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::GPU)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::GPU;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

GPUConfig Config::getGPUConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return gpuConfig_;
}

// ========== Loop Configuration ==========

void Config::setLoopConfig(const LoopConfig &config) {
  bool changed = false;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    if (loopConfig_ != config) {
      loopConfig_ = config;
      changed = true;

      if (immediateMode_) {
        for (const auto &entry : callbacks_) {
          if (hasFlag(entry.sections, ConfigSection::Loop)) {
            callbacksToNotify.push_back(entry);
          }
        }
      } else {
        pendingChanges_ = pendingChanges_ | ConfigSection::Loop;
      }
    }
  }

  if (changed && immediateMode_) {
    for (const auto &entry : callbacksToNotify) {
      entry.callback();
    }
  }
}

LoopConfig Config::getLoopConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return loopConfig_;
}

// ========== Change Callbacks ==========

bool Config::registerChangeCallback(const std::string &name,
                                    ConfigSection sections,
                                    ConfigChangeCallback callback) {
  std::lock_guard<std::mutex> lock(configMutex_);

  // Check if callback with this name already exists
  for (const auto &entry : callbacks_) {
    if (entry.name == name) {
      return false;
    }
  }

  callbacks_.push_back(CallbackEntry(name, sections, std::move(callback)));
  return true;
}

bool Config::unregisterChangeCallback(const std::string &name) {
  std::lock_guard<std::mutex> lock(configMutex_);

  auto it = std::find_if(
      callbacks_.begin(), callbacks_.end(),
      [&name](const CallbackEntry &entry) { return entry.name == name; });

  if (it != callbacks_.end()) {
    callbacks_.erase(it);
    return true;
  }

  return false;
}

std::vector<std::string> Config::getCallbackNames() const {
  std::lock_guard<std::mutex> lock(configMutex_);

  std::vector<std::string> names;
  names.reserve(callbacks_.size());
  for (const auto &entry : callbacks_) {
    names.push_back(entry.name);
  }
  return names;
}

void Config::applyPendingChanges() {
  ConfigSection changes;
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    changes = pendingChanges_;
    pendingChanges_ = ConfigSection::None;

    if (changes != ConfigSection::None) {
      for (const auto &entry : callbacks_) {
        if (hasFlag(changes, entry.sections)) {
          callbacksToNotify.push_back(entry);
        }
      }
    }
  }

  // Notify callbacks outside lock
  for (const auto &entry : callbacksToNotify) {
    entry.callback();
  }
}

void Config::setImmediateMode(bool immediate) {
  std::lock_guard<std::mutex> lock(configMutex_);
  immediateMode_ = immediate;
}

bool Config::isImmediateMode() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return immediateMode_;
}

void Config::notifyCallbacks(ConfigSection changedSections) {
  std::vector<CallbackEntry> callbacksToNotify;

  {
    std::lock_guard<std::mutex> lock(configMutex_);
    for (const auto &entry : callbacks_) {
      if (hasFlag(changedSections, entry.sections)) {
        callbacksToNotify.push_back(entry);
      }
    }
  }

  // Notify callbacks outside lock
  for (const auto &entry : callbacksToNotify) {
    entry.callback();
  }
}

} // namespace core
