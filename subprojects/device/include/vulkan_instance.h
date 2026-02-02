#ifndef VULKAN_INSTANCE_H_
#define VULKAN_INSTANCE_H_

#include "device_export.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

// Forward declare VMA types to avoid including heavy headers in the public API
// VMA uses dynamic Vulkan functions - no static linking required
struct VmaAllocator_T;
typedef VmaAllocator_T *VmaAllocator;
struct VmaAllocation_T;
typedef VmaAllocation_T *VmaAllocation;

namespace device {

/**
 * @brief Requirements for Vulkan instance creation
 */
struct DEVICE_API VulkanRequirements {
  std::vector<std::string> requiredExtensions;
  std::vector<std::string> requiredLayers;
};

/**
 * @brief Information about a physical GPU device
 */
struct DEVICE_API GPUInfo {
  vk::PhysicalDevice physicalDevice;
  vk::PhysicalDeviceProperties properties;
  vk::PhysicalDeviceMemoryProperties memoryProperties;
  vk::PhysicalDeviceFeatures features;
  std::vector<vk::QueueFamilyProperties> queueFamilies;

  uint32_t graphicsQueueFamily = UINT32_MAX;
  uint32_t computeQueueFamily = UINT32_MAX;
  uint32_t transferQueueFamily = UINT32_MAX;

  bool supportsGraphics() const { return graphicsQueueFamily != UINT32_MAX; }
  bool supportsCompute() const { return computeQueueFamily != UINT32_MAX; }
  bool supportsTransfer() const { return transferQueueFamily != UINT32_MAX; }

  std::string getName() const {
    return std::string(properties.deviceName.data());
  }

  bool isDiscrete() const {
    return properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
  }

  bool isIntegrated() const {
    return properties.deviceType == vk::PhysicalDeviceType::eIntegratedGpu;
  }
};

/**
 * @brief Represents a logical device with VMA allocator
 */
struct DEVICE_API LogicalDevice {
  std::unique_ptr<vk::raii::Device> device;
  std::unique_ptr<vk::raii::Queue> graphicsQueue;
  std::unique_ptr<vk::raii::Queue> computeQueue;
  std::unique_ptr<vk::raii::Queue> transferQueue;
  VmaAllocator allocator = nullptr;
  uint32_t gpuIndex = 0;

  bool isValid() const { return device != nullptr && allocator != nullptr; }
};

/**
 * @brief Buffer usage hints for VMA allocation
 */
enum class BufferUsage : uint8_t {
  GPU_ONLY,          // Fast GPU memory, not CPU accessible
  CPU_TO_GPU,        // Staging buffers for uploading
  GPU_TO_CPU,        // Readback buffers
  CPU_ONLY           // CPU memory, coherent access
};

/**
 * @brief RAII buffer wrapper with VMA allocation
 */
class DEVICE_API Buffer {
public:
  Buffer() = default;
  Buffer(vk::Buffer buffer, VmaAllocation allocation, VmaAllocator allocator,
         vk::DeviceSize size, BufferUsage usage);
  ~Buffer();

  // Move-only
  Buffer(Buffer &&other) noexcept;
  Buffer &operator=(Buffer &&other) noexcept;
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  [[nodiscard]] vk::Buffer getBuffer() const { return buffer_; }
  [[nodiscard]] VmaAllocation getAllocation() const { return allocation_; }
  [[nodiscard]] vk::DeviceSize getSize() const { return size_; }
  [[nodiscard]] BufferUsage getUsage() const { return usage_; }
  [[nodiscard]] bool isValid() const { return buffer_ && allocation_; }

  // Memory mapping
  void *map();
  void unmap();
  void flush(vk::DeviceSize offset = 0, vk::DeviceSize size = VK_WHOLE_SIZE);
  void invalidate(vk::DeviceSize offset = 0,
                  vk::DeviceSize size = VK_WHOLE_SIZE);

private:
  void destroy();

  vk::Buffer buffer_ = nullptr;
  VmaAllocation allocation_ = nullptr;
  VmaAllocator allocator_ = nullptr;
  vk::DeviceSize size_ = 0;
  BufferUsage usage_ = BufferUsage::GPU_ONLY;
  void *mappedData_ = nullptr;
};

/**
 * @brief Multi-GPU manager
 *
 * Manages multiple GPUs with their own logical devices and VMA allocators.
 * Supports work distribution across GPUs using thread pools.
 */
class DEVICE_API GPUManager {
public:
  GPUManager() = default;
  ~GPUManager();

  // Non-copyable
  GPUManager(const GPUManager &) = delete;
  GPUManager &operator=(const GPUManager &) = delete;

  // Move operations
  GPUManager(GPUManager &&other) noexcept;
  GPUManager &operator=(GPUManager &&other) noexcept;

  /**
   * @brief Initialize GPU manager with the Vulkan instance
   * @param instance RAII Vulkan instance
   * @param deviceExtensions Required device extensions
   * @return true if at least one GPU was initialized
   */
  bool initialize(const vk::raii::Instance &instance,
                  const std::vector<std::string> &deviceExtensions);

  /**
   * @brief Shutdown and cleanup all resources
   */
  void shutdown();

  /**
   * @brief Get available GPUs
   * @return Vector of GPU information
   */
  [[nodiscard]] const std::vector<GPUInfo> &getAvailableGPUs() const {
    return availableGPUs_;
  }

  /**
   * @brief Get active logical devices
   * @return Vector of logical devices
   */
  [[nodiscard]] const std::vector<LogicalDevice> &getDevices() const {
    return devices_;
  }

  /**
   * @brief Get primary device (first initialized device)
   * @return Pointer to primary device or nullptr
   */
  [[nodiscard]] LogicalDevice *getPrimaryDevice();

  /**
   * @brief Get device by index
   * @param index Device index
   * @return Pointer to device or nullptr
   */
  [[nodiscard]] LogicalDevice *getDevice(uint32_t index);

  /**
   * @brief Get number of active devices
   */
  [[nodiscard]] uint32_t getDeviceCount() const {
    return static_cast<uint32_t>(devices_.size());
  }

  /**
   * @brief Create a buffer on specified device
   * @param deviceIndex Device index
   * @param size Buffer size in bytes
   * @param bufferUsage Vulkan buffer usage flags
   * @param memoryUsage Memory usage hint
   * @return Created buffer or empty buffer on failure
   */
  Buffer createBuffer(uint32_t deviceIndex, vk::DeviceSize size,
                      vk::BufferUsageFlags bufferUsage,
                      BufferUsage memoryUsage);

  /**
   * @brief Create a buffer on primary device
   */
  Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage,
                      BufferUsage memoryUsage);

  /**
   * @brief Submit work to GPU thread pool
   * @param deviceIndex Device index
   * @param work Work function to execute
   */
  void submitWork(uint32_t deviceIndex, std::function<void()> work);

private:
  bool createLogicalDevice(const vk::raii::Instance &instance,
                           const GPUInfo &gpuInfo,
                           const std::vector<std::string> &deviceExtensions);
  bool createVMAAllocator(LogicalDevice &device,
                          const vk::raii::Instance &instance,
                          const GPUInfo &gpuInfo);
  void findQueueFamilies(GPUInfo &gpuInfo);

  std::vector<GPUInfo> availableGPUs_;
  std::vector<LogicalDevice> devices_;
  bool initialized_ = false;
};

/**
 * @brief Manages Vulkan instance and multi-GPU support
 *
 * Uses vk::raii wrappers for automatic resource management.
 * Responsible for:
 * - Creating and destroying Vulkan instance
 * - Loading Vulkan function pointers via dynamic dispatch
 * - Managing multiple GPUs with VMA allocators
 * - Buffer creation and management
 */
class DEVICE_API VulkanInstance {
public:
  VulkanInstance();
  ~VulkanInstance();

  // Disable copy
  VulkanInstance(const VulkanInstance &) = delete;
  VulkanInstance &operator=(const VulkanInstance &) = delete;

  // Enable move
  VulkanInstance(VulkanInstance &&other) noexcept;
  VulkanInstance &operator=(VulkanInstance &&other) noexcept;

  /**
   * @brief Initialize the Vulkan instance
   * @param requirements Requirements for instance creation
   * @return true if initialization succeeded
   */
  bool initialize(const VulkanRequirements &requirements);

  /**
   * @brief Initialize using configuration from Config class
   * @return true if initialization succeeded
   */
  bool initializeFromConfig();

  /**
   * @brief Shutdown the Vulkan instance and all resources
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
  [[nodiscard]] vk::Instance getInstance() const {
    return instance_ ? *instance_ : vk::Instance{};
  }

  /**
   * @brief Get the RAII instance reference
   * @return Reference to the RAII instance
   */
  [[nodiscard]] const vk::raii::Instance &getRaiiInstance() const {
    return *instance_;
  }

  /**
   * @brief Get the RAII context reference
   * @return Reference to the RAII context
   */
  [[nodiscard]] const vk::raii::Context &getContext() const {
    return *context_;
  }

  /**
   * @brief Get the GPU manager
   * @return Reference to GPU manager
   */
  [[nodiscard]] GPUManager &getGPUManager() { return gpuManager_; }
  [[nodiscard]] const GPUManager &getGPUManager() const { return gpuManager_; }

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
  static std::vector<std::string>
  checkExtensionSupport(const std::vector<std::string> &extensions);

  /**
   * @brief Check if required layers are supported
   * @param layers Layers to check
   * @return Vector of unsupported layer names (empty if all supported)
   */
  static std::vector<std::string>
  checkLayerSupport(const std::vector<std::string> &layers);

private:
  bool setupDebugMessenger();

  std::unique_ptr<vk::raii::Context> context_;
  std::unique_ptr<vk::raii::Instance> instance_;
  std::unique_ptr<vk::raii::DebugUtilsMessengerEXT> debugMessenger_;

  GPUManager gpuManager_;
  std::vector<std::string> deviceExtensions_;

  bool initialized_ = false;
};

} // namespace device

#endif // VULKAN_INSTANCE_H_
