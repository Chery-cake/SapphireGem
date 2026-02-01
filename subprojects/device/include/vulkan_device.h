#ifndef VULKAN_DEVICE_H_
#define VULKAN_DEVICE_H_

#include "device_export.h"
#include "vulkan_instance.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

namespace device {

/**
 * @brief Queue family indices for a physical device
 */
struct DEVICE_API QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> computeFamily;
    std::optional<uint32_t> transferFamily;
    std::optional<uint32_t> presentFamily;

    [[nodiscard]] bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }

    [[nodiscard]] bool hasCompute() const { return computeFamily.has_value(); }
    [[nodiscard]] bool hasTransfer() const { return transferFamily.has_value(); }
};

/**
 * @brief Information about a GPU device
 */
struct DEVICE_API GPUInfo {
    uint32_t index;
    std::string name;
    vk::PhysicalDeviceType type;
    uint32_t vendorId;
    uint32_t deviceId;
    vk::DeviceSize totalMemory;
    uint32_t apiVersion;
    uint32_t driverVersion;
    QueueFamilyIndices queueFamilies;
    bool supportsCompute;
    bool supportsTransfer;
    bool supportsPresent;
};

/**
 * @brief Configuration for logical device creation
 */
struct DEVICE_API VulkanDeviceConfig {
    std::vector<std::string> requiredExtensions;
    std::vector<std::string> optionalExtensions;
    bool enableMultiGPU = false;
    uint32_t preferredGPUIndex = 0;
    vk::SurfaceKHR surface = nullptr; // For presentation support detection
};

/**
 * @brief Represents a single GPU device with its logical device and queues using RAII
 *
 * Uses vk::raii wrappers for automatic resource management.
 */
class DEVICE_API GPUDevice {
public:
    GPUDevice() = default;
    ~GPUDevice();

    // Disable copy
    GPUDevice(const GPUDevice&) = delete;
    GPUDevice& operator=(const GPUDevice&) = delete;

    // Enable move
    GPUDevice(GPUDevice&& other) noexcept;
    GPUDevice& operator=(GPUDevice&& other) noexcept;

    /**
     * @brief Initialize this GPU device
     * @param instance The Vulkan RAII instance
     * @param physicalDevice The physical device to use
     * @param info GPU information
     * @param config Device configuration
     * @return true if initialization succeeded
     */
    bool initialize(const vk::raii::Instance& instance, 
                    vk::PhysicalDevice physicalDevice,
                    const GPUInfo& info,
                    const VulkanDeviceConfig& config);

    /**
     * @brief Shutdown and cleanup device resources
     */
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }
    [[nodiscard]] const GPUInfo& getInfo() const { return info_; }
    [[nodiscard]] vk::PhysicalDevice getPhysicalDevice() const { return physicalDevice_ ? *physicalDevice_ : vk::PhysicalDevice{}; }
    [[nodiscard]] vk::Device getDevice() const { return device_ ? *device_ : vk::Device{}; }
    [[nodiscard]] const vk::raii::Device& getRaiiDevice() const { return *device_; }
    [[nodiscard]] const vk::raii::PhysicalDevice& getRaiiPhysicalDevice() const { return *physicalDevice_; }
    [[nodiscard]] vk::Queue getGraphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] vk::Queue getComputeQueue() const { return computeQueue_; }
    [[nodiscard]] vk::Queue getTransferQueue() const { return transferQueue_; }
    [[nodiscard]] vk::Queue getPresentQueue() const { return presentQueue_; }
    [[nodiscard]] const QueueFamilyIndices& getQueueFamilies() const { return info_.queueFamilies; }

    /**
     * @brief Wait for the device to become idle
     */
    void waitIdle() const;

private:
    std::unique_ptr<vk::raii::PhysicalDevice> physicalDevice_;
    std::unique_ptr<vk::raii::Device> device_;
    GPUInfo info_;

    vk::Queue graphicsQueue_;
    vk::Queue computeQueue_;
    vk::Queue transferQueue_;
    vk::Queue presentQueue_;

    bool initialized_ = false;
};

/**
 * @brief Manages multiple GPU devices with multi-GPU support
 *
 * Responsible for:
 * - Enumerating physical devices
 * - Creating logical devices
 * - Managing queues across devices
 * - Supporting multi-GPU configurations
 */
class DEVICE_API VulkanDeviceManager {
public:
    VulkanDeviceManager();
    ~VulkanDeviceManager();

    // Disable copy
    VulkanDeviceManager(const VulkanDeviceManager&) = delete;
    VulkanDeviceManager& operator=(const VulkanDeviceManager&) = delete;

    /**
     * @brief Initialize the device manager
     * @param instance The Vulkan instance to use
     * @param config Device configuration
     * @return true if initialization succeeded
     */
    bool initialize(VulkanInstance& instance, const VulkanDeviceConfig& config);

    /**
     * @brief Shutdown and cleanup all devices
     */
    void shutdown();

    [[nodiscard]] bool isInitialized() const { return initialized_; }

    /**
     * @brief Get list of all available GPUs (before device creation)
     * @return Vector of GPU information
     */
    [[nodiscard]] const std::vector<GPUInfo>& getAvailableGPUs() const { return availableGPUs_; }

    /**
     * @brief Get the primary GPU device
     * @return Reference to the primary device
     */
    [[nodiscard]] GPUDevice& getPrimaryDevice();
    [[nodiscard]] const GPUDevice& getPrimaryDevice() const;

    /**
     * @brief Get a specific GPU device by index
     * @param index Device index
     * @return Pointer to device, or nullptr if not found
     */
    [[nodiscard]] GPUDevice* getDevice(uint32_t index);
    [[nodiscard]] const GPUDevice* getDevice(uint32_t index) const;

    /**
     * @brief Get all active devices
     * @return Vector of active devices
     */
    [[nodiscard]] const std::vector<std::unique_ptr<GPUDevice>>& getDevices() const { return devices_; }

    /**
     * @brief Get the number of active devices
     * @return Number of active GPU devices
     */
    [[nodiscard]] size_t getDeviceCount() const { return devices_.size(); }

    /**
     * @brief Check if multi-GPU mode is enabled
     * @return true if multiple GPUs are active
     */
    [[nodiscard]] bool isMultiGPUEnabled() const { return devices_.size() > 1; }

    /**
     * @brief Execute a function on all devices in parallel
     * @param func Function to execute (receives device reference and index)
     */
    void forEachDevice(const std::function<void(GPUDevice&, size_t)>& func);

private:
    void enumeratePhysicalDevices(const vk::raii::Instance& instance, vk::SurfaceKHR surface);
    GPUInfo queryDeviceInfo(vk::PhysicalDevice device, uint32_t index, vk::SurfaceKHR surface);
    QueueFamilyIndices findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface);
    int scoreDevice(const GPUInfo& info);

    std::vector<GPUInfo> availableGPUs_;
    std::vector<std::unique_ptr<GPUDevice>> devices_;
    VulkanInstance* vulkanInstance_ = nullptr;
    uint32_t primaryDeviceIndex_ = 0;
    bool initialized_ = false;
    mutable std::mutex deviceMutex_;
};

} // namespace device

#endif // VULKAN_DEVICE_H_
