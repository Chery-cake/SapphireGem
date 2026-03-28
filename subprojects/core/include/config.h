#ifndef CONFIG_H_
#define CONFIG_H_

#include "core_export.h"
#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace core {

// Forward Declaration
class VulkanConfig;
class ThreadsConfig;

/**
 *@brief Application specific configuration settings
 */
struct CORE_API ApplicationConfig {
  std::string applicationName = "SapphireEngine Tests";
  uint32_t applicationVersion = VK_MAKE_VERSION(0, 1, 0);

  bool operator==(const ApplicationConfig &other) const {
    return applicationName == other.applicationName &&
           applicationVersion == other.applicationVersion;
  }

  bool operator!=(const ApplicationConfig &other) const {
    return !(*this == other);
  }
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
class CORE_API Config {
public:
  using ConfigChangeCallback = std::function<void()>;

  // Singleton access
  static Config &instance();

  // Delete copy and move operations
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;
  Config(Config &&) = delete;
  Config &operator=(Config &&) = delete;

  // ========== Application Configuration ==========

  /**
   * @brief Set Application configuration
   * @param config New Application configuration
   */
  void setApplicationConfig(const ApplicationConfig &config);

  /**
   * @brief Get current Application configuration
   * @return Current Application configuration
   */
  const ApplicationConfig &getApplicationConfig() const;

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
  const VulkanConfig &getVulkanConfig() const;

  // ========== Threads Configuration ==========

  /**
   * @brief Set Threads configuration
   * @param config New Threads configuration
   *
   * Triggers Threads configuration change callbacks if values differ.
   */
  void setThreadsConfig(const ThreadsConfig &config);

  /**
   * @brief Get current Threads configuration
   * @return Current Threads configuration
   */
  const ThreadsConfig &getThreadsConfig() const;

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

  /**
   * @brief Callback entry with monitored sections
   */
  struct CallbackEntry {
    std::string name;
    ConfigSection sections;
    ConfigChangeCallback callback;
  };

#ifdef ENGINE_DEBUG
  // Hot reload support: set/get the singleton instance
  static void setInstance(Config *inst);
  static Config *getInstance();
  // In debug mode, allow direct instantiation for hot reload
  Config();
  ~Config();
#else
private:
  Config();
  ~Config();
#endif
private:
  /**
   * @brief Notify callbacks about configuration changes
   * @param changedSections Sections that have changed
   */
  void notifyCallbacks(ConfigSection changedSections);

  // Configuration data
  ApplicationConfig applicationConfig_;
  std::unique_ptr<VulkanConfig> vulkanConfig_;
  std::unique_ptr<ThreadsConfig> threadsConfig_;

  // Callbacks
  std::vector<CallbackEntry> callbacks_;

  // Pending changes tracking
  ConfigSection pendingChanges_ = ConfigSection::None;
  bool immediateMode_ = true;

  mutable std::mutex configMutex_;
};

} // namespace core

#endif // CONFIG_H_
