#ifndef CONFIG_VULKAN_H_
#define CONFIG_VULKAN_H_

#include "config.h"
#include "core_export.h"
#include "vulkan/vulkan_core.h"
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace core {

/**
 * @brief Vulkan-specific configuration settings
 */
class CORE_API VulkanConfig {
public:
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
   * @brief Add a Vulkan instance layer
   * @param layer Layer name to add
   */
  void addInstanceLayer(const std::string &layer);

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
   * @brief Remove a Vulkan instance layer
   * @param layer Layer name to remove
   * @return true if layer was removed
   */
  bool removeInstanceLayer(const std::string &layer);

  /**
   * @brief Add a optional Vulkan instance extension
   * @param extension Extension name to add
   */
  void addOptionalInstanceExtension(const std::string &extension);

  /**
   * @brief Add a optional Vulkan device extension
   * @param extension Extension name to add
   */
  void addOptionalDeviceExtension(const std::string &extension);

  /**
   * @brief Add a optional Vulkan instance layer
   * @param layer Layer name to add
   */
  void addOptionalInstanceLayer(const std::string &layer);

  /**
   * @brief Remove a optional Vulkan instance extension
   * @param extension Extension name to remove
   * @return true if extension was removed
   */
  bool removeOptionalInstanceExtension(const std::string &extension);

  /**
   * @brief Remove a optional Vulkan device extension
   * @param extension Extension name to remove
   * @return true if extension was removed
   */
  bool removeOptionalDeviceExtension(const std::string &extension);

  /**
   * @brief Remove a optional Vulkan instance layer
   * @param layer Layer name to remove
   * @return true if layer was removed
   */
  bool removeOptionalInstanceLayer(const std::string &layer);

  const std::vector<std::string> &getInstanceExtensions() const {
    return instanceExtensions;
  }

  const std::vector<std::string> &getDeviceExtensions() const {
    return deviceExtensions;
  }

  const std::vector<std::string> &getInstanceLayers() const {
    return instanceLayers;
  }

  const std::vector<std::string> &getOptionalInstanceExtensions() const {
    return optionalInstanceExtensions;
  }

  const std::vector<std::string> &getOptionalDeviceExtensions() const {
    return optionalDeviceExtensions;
  }

  const std::vector<std::string> &getOptionalInstanceLayers() const {
    return optionalInstanceLayers;
  }

  void setEngineName(std::string name) { engineName = name; }
  void setEngineVersion(uint32_t version) { engineVersion = version; }
  void setMinApiVersion(uint32_t version) { minApiVersion = version; }

  const std::string &getEngineName() const { return engineName; }
  const uint32_t &getEngineVersion() const { return engineVersion; }
  const uint32_t &getMinApiVersion() const { return minApiVersion; }

  bool operator==(const VulkanConfig &other) const {
    return instanceExtensions == other.instanceExtensions &&
           deviceExtensions == other.deviceExtensions &&
           instanceLayers == other.instanceLayers &&
           optionalInstanceExtensions == other.optionalInstanceExtensions &&
           optionalDeviceExtensions == other.optionalDeviceExtensions &&
           optionalInstanceLayers == other.optionalInstanceLayers &&
           engineName == other.engineName &&
           engineVersion == other.engineVersion &&
           minApiVersion == other.minApiVersion;
  }

  bool operator!=(const VulkanConfig &other) const { return !(*this == other); }

  VulkanConfig &operator=(const VulkanConfig &other) {
    if (this == &other)
      return *this; // Handle self-assignment

    instanceExtensions = other.instanceExtensions;
    deviceExtensions = other.deviceExtensions;
    instanceLayers = other.instanceLayers;
    optionalInstanceExtensions = other.optionalInstanceExtensions;
    optionalDeviceExtensions = other.optionalDeviceExtensions;
    optionalInstanceLayers = other.optionalInstanceLayers;
    engineName = other.engineName;
    engineVersion = other.engineVersion;
    minApiVersion = other.minApiVersion;

    return *this;
  }

  VulkanConfig &operator=(VulkanConfig &&other) noexcept {
    if (this == &other)
      return *this; // Handle self-assignment

    instanceExtensions = std::move(other.instanceExtensions);
    deviceExtensions = std::move(other.deviceExtensions);
    instanceLayers = std::move(other.instanceLayers);
    optionalInstanceExtensions = std::move(other.optionalInstanceExtensions);
    optionalDeviceExtensions = std::move(other.optionalDeviceExtensions);
    optionalInstanceLayers = std::move(other.optionalInstanceLayers);
    engineName = std::move(other.engineName);
    engineVersion = other.engineVersion;
    minApiVersion = other.minApiVersion;

    return *this;
  }

  explicit VulkanConfig(ConfigSection &pendingChanges, bool &immediateMode,
                        std::vector<Config::CallbackEntry> &callbacks,
                        std::mutex &configMutex);
  ~VulkanConfig();

  void resetToDefaults();

private:
  ConfigSection &pendingChanges;
  bool &immediateMode;
  std::vector<Config::CallbackEntry> &callbacks;
  std::mutex &configMutex;

  std::vector<std::string> instanceExtensions;
  std::vector<std::string> deviceExtensions;
  std::vector<std::string> instanceLayers;

  // TODO check functions to add and remove optional extensions and layer work
  // properly
  std::vector<std::string> optionalInstanceExtensions;
  std::vector<std::string> optionalDeviceExtensions;
  std::vector<std::string> optionalInstanceLayers;

  std::string engineName = "SapphireEngine";
  uint32_t engineVersion = VK_MAKE_VERSION(0, 1, 0);
  uint32_t minApiVersion = VK_API_VERSION_1_3;

  mutable std::mutex configVulkanMutex_;
};

} // namespace core

#endif // CONFIG_VULKAN_H_
