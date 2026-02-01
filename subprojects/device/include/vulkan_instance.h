#ifndef VULKAN_INSTANCE_H_
#define VULKAN_INSTANCE_H_

#include "device_export.h"
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

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
 * @brief Manages Vulkan instance and validation layers using RAII
 *
 * Uses vk::raii wrappers for automatic resource management.
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
     * @brief Get the Vulkan instance handle (raw handle for interop)
     * @return Vulkan instance
     */
    [[nodiscard]] vk::Instance getInstance() const { return instance_ ? *instance_ : vk::Instance{}; }

    /**
     * @brief Get the RAII instance reference
     * @return Reference to the RAII instance
     */
    [[nodiscard]] const vk::raii::Instance& getRaiiInstance() const { return *instance_; }

    /**
     * @brief Get the RAII context reference
     * @return Reference to the RAII context
     */
    [[nodiscard]] const vk::raii::Context& getContext() const { return *context_; }

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

    std::unique_ptr<vk::raii::Context> context_;
    std::unique_ptr<vk::raii::Instance> instance_;
    std::unique_ptr<vk::raii::DebugUtilsMessengerEXT> debugMessenger_;
    
    bool initialized_ = false;
    bool hasLayers_ = false;
};

} // namespace device

#endif // VULKAN_INSTANCE_H_
