#ifndef VULKAN_INSTANCE_H_
#define VULKAN_INSTANCE_H_

#include "device_export.h"
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace device {

/**
 * @brief Configuration for Vulkan instance creation
 */
struct DEVICE_API VulkanInstanceConfig {
    std::string applicationName = "SapphireEngine";
    uint32_t applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    std::string engineName = "SapphireEngine";
    uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0);
    uint32_t apiVersion = VK_API_VERSION_1_3;

    std::vector<std::string> requiredExtensions;
    std::vector<std::string> instanceLayers; // Instance layers (validation layers in debug builds)
};

/**
 * @brief Manages Vulkan instance and validation layers
 *
 * Responsible for:
 * - Creating and destroying Vulkan instance
 * - Managing validation layers (debug builds)
 * - Loading Vulkan function pointers via dynamic dispatch
 */
class DEVICE_API VulkanInstance {
public:
    VulkanInstance();
    ~VulkanInstance();

    // Disable copy
    VulkanInstance(const VulkanInstance&) = delete;
    VulkanInstance& operator=(const VulkanInstance&) = delete;

    // Enable move
    VulkanInstance(VulkanInstance&& other) noexcept;
    VulkanInstance& operator=(VulkanInstance&& other) noexcept;

    /**
     * @brief Initialize the Vulkan instance
     * @param config Configuration for instance creation
     * @return true if initialization succeeded
     */
    bool initialize(const VulkanInstanceConfig& config);

    /**
     * @brief Shutdown and cleanup Vulkan resources
     */
    void shutdown();

    /**
     * @brief Check if the instance is initialized
     * @return true if initialized
     */
    [[nodiscard]] bool isInitialized() const { return initialized_; }

    /**
     * @brief Get the Vulkan instance handle
     * @return Vulkan instance
     */
    [[nodiscard]] vk::Instance getInstance() const { return instance_; }

    /**
     * @brief Get the dynamic dispatch loader
     * @return Reference to the dispatch loader
     */
    [[nodiscard]] const vk::detail::DynamicLoader& getDynamicLoader() const { return dynamicLoader_; }

    /**
     * @brief Check if instance layers are enabled (has any layers)
     * @return true if layers are enabled
     */
    [[nodiscard]] bool hasInstanceLayers() const { return hasLayers_; }

    /**
     * @brief Get list of available instance extensions
     * @return Vector of extension names
     */
    static std::vector<std::string> getAvailableExtensions();

    /**
     * @brief Get list of available instance layers
     * @return Vector of layer names
     */
    static std::vector<std::string> getAvailableLayers();

    /**
     * @brief Check if required extensions are supported
     * @param extensions Extensions to check
     * @return Vector of unsupported extension names (empty if all supported)
     */
    static std::vector<std::string> checkExtensionSupport(
        const std::vector<std::string>& extensions);

    /**
     * @brief Check if required layers are supported
     * @param layers Layers to check
     * @return Vector of unsupported layer names (empty if all supported)
     */
    static std::vector<std::string> checkLayerSupport(
        const std::vector<std::string>& layers);

private:
    bool setupDebugMessenger();
    void destroyDebugMessenger();

    vk::detail::DynamicLoader dynamicLoader_;
    vk::Instance instance_;
    vk::DebugUtilsMessengerEXT debugMessenger_;
    
    bool initialized_ = false;
    bool hasLayers_ = false;
};

} // namespace device

#endif // VULKAN_INSTANCE_H_
