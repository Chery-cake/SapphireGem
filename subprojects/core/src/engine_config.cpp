#include "engine_config.h"
#include <algorithm>
#include <thread>

namespace core {

#ifdef ENGINE_DEBUG
static EngineConfig *g_engineConfigInstance = nullptr;
#endif

EngineConfig &EngineConfig::instance() {
#ifdef ENGINE_DEBUG
  if (g_engineConfigInstance) {
    return *g_engineConfigInstance;
  }
#endif
  static EngineConfig instance;
#ifdef ENGINE_DEBUG
  g_engineConfigInstance = &instance;
#endif
  return instance;
}

#ifdef ENGINE_DEBUG
void EngineConfig::setInstance(EngineConfig *inst) {
  g_engineConfigInstance = inst;
}

EngineConfig *EngineConfig::getInstance() { return g_engineConfigInstance; }
#endif

EngineConfig::EngineConfig() {
  // Initialize with sensible defaults
  vulkanConfig_.enableValidation = true;

  // Default validation layers
  vulkanConfig_.validationLayers.push_back("VK_LAYER_KHRONOS_validation");

  // Common instance extensions
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_surface");
#ifdef _WIN32
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_win32_surface");
#elif defined(__linux__)
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_xcb_surface");
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_wayland_surface");
#elif defined(__APPLE__)
  vulkanConfig_.instanceExtensions.push_back("VK_EXT_metal_surface");
#endif

  // Common device extensions
  vulkanConfig_.deviceExtensions.push_back("VK_KHR_swapchain");

  // Auto-detect thread count
  threadPoolAllocation_.workerThreads = 0; // Will be calculated based on hardware
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

EngineConfig::~EngineConfig() { shutdown(); }

void EngineConfig::shutdown() {
  std::lock_guard<std::mutex> lock(configMutex_);
  callbacks_.clear();
  pendingChanges_ = ConfigSection::None;
}

void EngineConfig::resetToDefaults() {
  std::lock_guard<std::mutex> lock(configMutex_);

  vulkanConfig_ = VulkanConfig{};
  vulkanConfig_.enableValidation = true;
  vulkanConfig_.validationLayers.push_back("VK_LAYER_KHRONOS_validation");
  vulkanConfig_.instanceExtensions.push_back("VK_KHR_surface");
  vulkanConfig_.deviceExtensions.push_back("VK_KHR_swapchain");

  threadPoolAllocation_ = ThreadPoolAllocation{};
  gpuConfig_ = GPUConfig{};
  loopConfig_ = LoopConfig{};

  pendingChanges_ = ConfigSection::All;

  if (immediateMode_) {
    // Copy callbacks to notify outside lock
    auto callbacksCopy = callbacks_;
    configMutex_.unlock();
    for (const auto &entry : callbacksCopy) {
      if (hasFlag(pendingChanges_, entry.sections)) {
        entry.callback();
      }
    }
    configMutex_.lock();
    pendingChanges_ = ConfigSection::None;
  }
}

// ========== Vulkan Configuration ==========

void EngineConfig::setVulkanConfig(const VulkanConfig &config) {
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

VulkanConfig EngineConfig::getVulkanConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return vulkanConfig_;
}

void EngineConfig::addInstanceExtension(const std::string &extension) {
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

void EngineConfig::addDeviceExtension(const std::string &extension) {
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

void EngineConfig::addValidationLayer(const std::string &layer) {
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

bool EngineConfig::removeInstanceExtension(const std::string &extension) {
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

bool EngineConfig::removeDeviceExtension(const std::string &extension) {
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

bool EngineConfig::removeValidationLayer(const std::string &layer) {
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

void EngineConfig::setValidationEnabled(bool enable) {
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

void EngineConfig::setThreadPoolAllocation(
    const ThreadPoolAllocation &allocation) {
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

ThreadPoolAllocation EngineConfig::getThreadPoolAllocation() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return threadPoolAllocation_;
}

ThreadPoolAllocation EngineConfig::getEffectiveThreadAllocation() const {
  std::lock_guard<std::mutex> lock(configMutex_);

  ThreadPoolAllocation effective = threadPoolAllocation_;

  // Calculate effective worker threads if set to auto-detect
  if (effective.workerThreads == 0) {
    uint32_t hardwareThreads =
        std::max(1u, std::thread::hardware_concurrency());

    // Reserve threads for loops and GPUs
    uint32_t reservedForLoops = effective.loopThreads * loopConfig_.mainLoopCount;
    uint32_t reservedForGPUs = effective.gpuThreads * gpuConfig_.gpuCount;
    uint32_t totalReserved = reservedForLoops + reservedForGPUs;

    // Ensure we have at least 1 worker thread
    effective.workerThreads =
        (hardwareThreads > totalReserved) ? (hardwareThreads - totalReserved) : 1;
  }

  // Adjust GPU threads based on actual GPU count
  if (gpuConfig_.enableMultiGPU) {
    effective.gpuThreads = effective.gpuThreads * gpuConfig_.gpuCount;
  }

  // Adjust loop threads based on main loop count
  effective.loopThreads = effective.loopThreads * loopConfig_.mainLoopCount;

  return effective;
}

// ========== GPU Configuration ==========

void EngineConfig::setGPUConfig(const GPUConfig &config) {
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

GPUConfig EngineConfig::getGPUConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return gpuConfig_;
}

// ========== Loop Configuration ==========

void EngineConfig::setLoopConfig(const LoopConfig &config) {
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

LoopConfig EngineConfig::getLoopConfig() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return loopConfig_;
}

// ========== Change Callbacks ==========

bool EngineConfig::registerChangeCallback(const std::string &name,
                                          ConfigSection sections,
                                          ConfigChangeCallback callback) {
  std::lock_guard<std::mutex> lock(configMutex_);

  // Check if callback with this name already exists
  for (const auto &entry : callbacks_) {
    if (entry.name == name) {
      return false;
    }
  }

  callbacks_.push_back(CallbackEntry{name, sections, std::move(callback)});
  return true;
}

bool EngineConfig::unregisterChangeCallback(const std::string &name) {
  std::lock_guard<std::mutex> lock(configMutex_);

  auto it =
      std::find_if(callbacks_.begin(), callbacks_.end(),
                   [&name](const CallbackEntry &entry) { return entry.name == name; });

  if (it != callbacks_.end()) {
    callbacks_.erase(it);
    return true;
  }

  return false;
}

std::vector<std::string> EngineConfig::getCallbackNames() const {
  std::lock_guard<std::mutex> lock(configMutex_);

  std::vector<std::string> names;
  names.reserve(callbacks_.size());
  for (const auto &entry : callbacks_) {
    names.push_back(entry.name);
  }
  return names;
}

void EngineConfig::applyPendingChanges() {
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

void EngineConfig::setImmediateMode(bool immediate) {
  std::lock_guard<std::mutex> lock(configMutex_);
  immediateMode_ = immediate;
}

bool EngineConfig::isImmediateMode() const {
  std::lock_guard<std::mutex> lock(configMutex_);
  return immediateMode_;
}

void EngineConfig::notifyCallbacks(ConfigSection changedSections) {
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
