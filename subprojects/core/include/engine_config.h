#ifndef ENGINE_CONFIG_H_
#define ENGINE_CONFIG_H_

#include "core_export.h"
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace core {

/**
 * @brief Configuration callback type for when configuration changes
 */
using ConfigChangeCallback = std::function<void()>;

/**
 * @brief Vulkan-specific configuration settings
 */
struct CORE_API VulkanConfig {
  std::vector<std::string> instanceExtensions;
  std::vector<std::string> deviceExtensions;
  std::vector<std::string> validationLayers;
  bool enableValidation = true;

  bool operator==(const VulkanConfig &other) const {
    return instanceExtensions == other.instanceExtensions &&
           deviceExtensions == other.deviceExtensions &&
           validationLayers == other.validationLayers &&
           enableValidation == other.enableValidation;
  }

  bool operator!=(const VulkanConfig &other) const { return !(*this == other); }
};

/**
 * @brief Thread pool allocation configuration
 *
 * Specifies how many threads should be dedicated to each type of pool,
 * adjusting for the number of loops and GPUs.
 */
struct CORE_API ThreadPoolAllocation {
  uint32_t workerThreads = 0;   // 0 = auto-detect based on hardware
  uint32_t loopThreads = 1;     // Threads for main loop callbacks
  uint32_t gpuThreads = 0;      // Threads per GPU for GPU operations

  bool operator==(const ThreadPoolAllocation &other) const {
    return workerThreads == other.workerThreads &&
           loopThreads == other.loopThreads && gpuThreads == other.gpuThreads;
  }

  bool operator!=(const ThreadPoolAllocation &other) const {
    return !(*this == other);
  }
};

/**
 * @brief GPU configuration settings
 */
struct CORE_API GPUConfig {
  uint32_t gpuCount = 1;            // Number of GPUs to use
  uint32_t preferredGPUIndex = 0;   // Preferred GPU index for primary rendering
  bool enableMultiGPU = false;      // Enable multi-GPU rendering

  bool operator==(const GPUConfig &other) const {
    return gpuCount == other.gpuCount &&
           preferredGPUIndex == other.preferredGPUIndex &&
           enableMultiGPU == other.enableMultiGPU;
  }

  bool operator!=(const GPUConfig &other) const { return !(*this == other); }
};

/**
 * @brief Loop configuration settings
 */
struct CORE_API LoopConfig {
  uint32_t mainLoopCount = 1;   // Number of main loops (e.g., for multi-window)
  uint32_t targetFrameRate = 60;  // Target frame rate (0 = unlimited)
  bool enableVSync = true;        // Enable vertical sync

  bool operator==(const LoopConfig &other) const {
    return mainLoopCount == other.mainLoopCount &&
           targetFrameRate == other.targetFrameRate &&
           enableVSync == other.enableVSync;
  }

  bool operator!=(const LoopConfig &other) const { return !(*this == other); }
};

/**
 * @brief Configuration change flags to track what has changed
 */
enum class ConfigSection : uint8_t {
  None = 0,
  Vulkan = 1 << 0,
  ThreadPool = 1 << 1,
  GPU = 1 << 2,
  Loop = 1 << 3,
  All = Vulkan | ThreadPool | GPU | Loop
};

inline ConfigSection operator|(ConfigSection a, ConfigSection b) {
  return static_cast<ConfigSection>(static_cast<uint8_t>(a) |
                                    static_cast<uint8_t>(b));
}

inline ConfigSection operator&(ConfigSection a, ConfigSection b) {
  return static_cast<ConfigSection>(static_cast<uint8_t>(a) &
                                    static_cast<uint8_t>(b));
}

inline bool hasFlag(ConfigSection flags, ConfigSection flag) {
  return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) != 0;
}

/**
 * @brief Engine configuration singleton
 *
 * Manages all important engine configurations including:
 * - Vulkan extensions and layers
 * - Thread pool allocations for worker, loop, and GPU pools
 * - GPU configuration for multi-GPU support
 * - Loop configuration for frame rate and vsync
 *
 * When configurations change, registered callbacks are invoked to update
 * the system to the new settings.
 */
class CORE_API EngineConfig {
public:
  // Singleton access
  static EngineConfig &instance();

  // Delete copy and move operations
  EngineConfig(const EngineConfig &) = delete;
  EngineConfig &operator=(const EngineConfig &) = delete;
  EngineConfig(EngineConfig &&) = delete;
  EngineConfig &operator=(EngineConfig &&) = delete;

  // ========== Vulkan Configuration ==========

  /**
   * @brief Set Vulkan configuration
   * @param config New Vulkan configuration
   *
   * Triggers Vulkan configuration change callbacks if values differ.
   */
  void setVulkanConfig(const VulkanConfig &config);

  /**
   * @brief Get current Vulkan configuration
   * @return Current Vulkan configuration
   */
  VulkanConfig getVulkanConfig() const;

  /**
   * @brief Add a Vulkan instance extension
   * @param extension Extension name to add
   */
  void addInstanceExtension(const std::string &extension);

  /**
   * @brief Add a Vulkan device extension
   * @param extension Extension name to add
   */
  void addDeviceExtension(const std::string &extension);

  /**
   * @brief Add a Vulkan validation layer
   * @param layer Layer name to add
   */
  void addValidationLayer(const std::string &layer);

  /**
   * @brief Remove a Vulkan instance extension
   * @param extension Extension name to remove
   * @return true if extension was removed
   */
  bool removeInstanceExtension(const std::string &extension);

  /**
   * @brief Remove a Vulkan device extension
   * @param extension Extension name to remove
   * @return true if extension was removed
   */
  bool removeDeviceExtension(const std::string &extension);

  /**
   * @brief Remove a Vulkan validation layer
   * @param layer Layer name to remove
   * @return true if layer was removed
   */
  bool removeValidationLayer(const std::string &layer);

  /**
   * @brief Enable or disable Vulkan validation
   * @param enable Whether to enable validation
   */
  void setValidationEnabled(bool enable);

  // ========== Thread Pool Configuration ==========

  /**
   * @brief Set thread pool allocation
   * @param allocation New thread pool allocation
   *
   * Triggers thread pool configuration change callbacks if values differ.
   */
  void setThreadPoolAllocation(const ThreadPoolAllocation &allocation);

  /**
   * @brief Get current thread pool allocation
   * @return Current thread pool allocation
   */
  ThreadPoolAllocation getThreadPoolAllocation() const;

  /**
   * @brief Calculate effective thread counts based on GPUs and loops
   * @return Calculated thread counts considering hardware
   */
  ThreadPoolAllocation getEffectiveThreadAllocation() const;

  // ========== GPU Configuration ==========

  /**
   * @brief Set GPU configuration
   * @param config New GPU configuration
   *
   * Triggers GPU configuration change callbacks if values differ.
   */
  void setGPUConfig(const GPUConfig &config);

  /**
   * @brief Get current GPU configuration
   * @return Current GPU configuration
   */
  GPUConfig getGPUConfig() const;

  // ========== Loop Configuration ==========

  /**
   * @brief Set loop configuration
   * @param config New loop configuration
   *
   * Triggers loop configuration change callbacks if values differ.
   */
  void setLoopConfig(const LoopConfig &config);

  /**
   * @brief Get current loop configuration
   * @return Current loop configuration
   */
  LoopConfig getLoopConfig() const;

  // ========== Change Callbacks ==========

  /**
   * @brief Register a callback for configuration changes
   * @param name Unique name for the callback
   * @param sections Which configuration sections to monitor
   * @param callback Function to call when monitored sections change
   * @return true if callback was registered
   */
  bool registerChangeCallback(const std::string &name, ConfigSection sections,
                              ConfigChangeCallback callback);

  /**
   * @brief Unregister a configuration change callback
   * @param name Name of the callback to remove
   * @return true if callback was removed
   */
  bool unregisterChangeCallback(const std::string &name);

  /**
   * @brief Get list of all registered callback names
   * @return Vector of callback names
   */
  std::vector<std::string> getCallbackNames() const;

  /**
   * @brief Apply all pending configuration changes
   *
   * Call this after making multiple configuration changes to trigger
   * callbacks only once.
   */
  void applyPendingChanges();

  /**
   * @brief Set whether changes should be applied immediately or batched
   * @param immediate If true, callbacks are invoked immediately on change
   */
  void setImmediateMode(bool immediate);

  /**
   * @brief Check if immediate mode is enabled
   * @return true if changes are applied immediately
   */
  bool isImmediateMode() const;

  // ========== Lifecycle ==========

  /**
   * @brief Shutdown the configuration manager
   */
  void shutdown();

  /**
   * @brief Reset all configurations to defaults
   */
  void resetToDefaults();

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(EngineConfig *inst);
  static EngineConfig *getInstance();
  // In debug mode, allow direct instantiation for hot reload
  EngineConfig();
  ~EngineConfig();
#else
private:
  EngineConfig();
  ~EngineConfig();
#endif

private:
  /**
   * @brief Notify callbacks about configuration changes
   * @param changedSections Sections that have changed
   */
  void notifyCallbacks(ConfigSection changedSections);

  // Callback entry with monitored sections
  struct CallbackEntry {
    std::string name;
    ConfigSection sections;
    ConfigChangeCallback callback;
  };

  // Configuration data
  VulkanConfig vulkanConfig_;
  ThreadPoolAllocation threadPoolAllocation_;
  GPUConfig gpuConfig_;
  LoopConfig loopConfig_;

  // Callbacks
  std::vector<CallbackEntry> callbacks_;

  // Pending changes tracking
  ConfigSection pendingChanges_ = ConfigSection::None;
  bool immediateMode_ = true;

  mutable std::mutex configMutex_;
};

} // namespace core

#endif // ENGINE_CONFIG_H_
