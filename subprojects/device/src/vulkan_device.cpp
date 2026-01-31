#include "vulkan_device.h"
#include "thread_manager.h"
#include <algorithm>
#include <iostream>
#include <set>

namespace device {

// ============================================================================
// GPUDevice Implementation
// ============================================================================

GPUDevice::~GPUDevice() {
    shutdown();
}

GPUDevice::GPUDevice(GPUDevice&& other) noexcept
    : physicalDevice_(other.physicalDevice_)
    , device_(other.device_)
    , info_(std::move(other.info_))
    , graphicsQueue_(other.graphicsQueue_)
    , computeQueue_(other.computeQueue_)
    , transferQueue_(other.transferQueue_)
    , presentQueue_(other.presentQueue_)
    , initialized_(other.initialized_) {
    other.device_ = nullptr;
    other.initialized_ = false;
}

GPUDevice& GPUDevice::operator=(GPUDevice&& other) noexcept {
    if (this != &other) {
        shutdown();
        physicalDevice_ = other.physicalDevice_;
        device_ = other.device_;
        info_ = std::move(other.info_);
        graphicsQueue_ = other.graphicsQueue_;
        computeQueue_ = other.computeQueue_;
        transferQueue_ = other.transferQueue_;
        presentQueue_ = other.presentQueue_;
        initialized_ = other.initialized_;
        other.device_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

bool GPUDevice::initialize(vk::Instance instance,
                           vk::PhysicalDevice physicalDevice,
                           const GPUInfo& info,
                           const VulkanDeviceConfig& config) {
    (void)instance;
    if (initialized_) {
        std::cerr << "[GPUDevice] Already initialized" << std::endl;
        return false;
    }

    physicalDevice_ = physicalDevice;
    info_ = info;

    // Collect unique queue families
    std::set<uint32_t> uniqueQueueFamilies;
    if (info.queueFamilies.graphicsFamily.has_value()) {
        uniqueQueueFamilies.insert(info.queueFamilies.graphicsFamily.value());
    }
    if (info.queueFamilies.presentFamily.has_value()) {
        uniqueQueueFamilies.insert(info.queueFamilies.presentFamily.value());
    }
    if (info.queueFamilies.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(info.queueFamilies.computeFamily.value());
    }
    if (info.queueFamilies.transferFamily.has_value()) {
        uniqueQueueFamilies.insert(info.queueFamilies.transferFamily.value());
    }

    // Create queue create infos
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t family : uniqueQueueFamilies) {
        vk::DeviceQueueCreateInfo queueCreateInfo{
            {},
            family,
            1,
            &queuePriority
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Get device features
    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE;
    deviceFeatures.wideLines = VK_TRUE;
    deviceFeatures.geometryShader = VK_TRUE;
    deviceFeatures.tessellationShader = VK_TRUE;

    // Prepare extensions
    std::vector<const char*> enabledExtensions;
    for (const auto& ext : config.requiredExtensions) {
        enabledExtensions.push_back(ext.c_str());
    }

    // Check optional extensions
    auto availableExtensions = physicalDevice_.enumerateDeviceExtensionProperties();
    for (const auto& optExt : config.optionalExtensions) {
        auto it = std::find_if(availableExtensions.begin(), availableExtensions.end(),
            [&optExt](const vk::ExtensionProperties& props) {
                return optExt == props.extensionName.data();
            });
        if (it != availableExtensions.end()) {
            enabledExtensions.push_back(optExt.c_str());
        }
    }

    // Create logical device
    vk::DeviceCreateInfo createInfo{
        {},
        queueCreateInfos,
        {},  // No layers for device
        enabledExtensions,
        &deviceFeatures
    };

    try {
        device_ = physicalDevice_.createDevice(createInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[GPUDevice] Failed to create logical device: " << e.what() << std::endl;
        return false;
    }

    // Initialize dispatch loader with device
    VULKAN_HPP_DEFAULT_DISPATCHER.init(device_);

    // Get queues
    if (info.queueFamilies.graphicsFamily.has_value()) {
        graphicsQueue_ = device_.getQueue(info.queueFamilies.graphicsFamily.value(), 0);
    }
    if (info.queueFamilies.presentFamily.has_value()) {
        presentQueue_ = device_.getQueue(info.queueFamilies.presentFamily.value(), 0);
    }
    if (info.queueFamilies.computeFamily.has_value()) {
        computeQueue_ = device_.getQueue(info.queueFamilies.computeFamily.value(), 0);
    }
    if (info.queueFamilies.transferFamily.has_value()) {
        transferQueue_ = device_.getQueue(info.queueFamilies.transferFamily.value(), 0);
    }

    initialized_ = true;
    std::cout << "[GPUDevice] Initialized: " << info.name << std::endl;
    return true;
}

void GPUDevice::shutdown() {
    if (!initialized_) {
        return;
    }

    if (device_) {
        device_.waitIdle();
        device_.destroy();
        device_ = nullptr;
    }

    initialized_ = false;
    std::cout << "[GPUDevice] Shutdown: " << info_.name << std::endl;
}

void GPUDevice::waitIdle() const {
    if (device_) {
        device_.waitIdle();
    }
}

// ============================================================================
// VulkanDeviceManager Implementation
// ============================================================================

VulkanDeviceManager::VulkanDeviceManager() = default;

VulkanDeviceManager::~VulkanDeviceManager() {
    shutdown();
}

bool VulkanDeviceManager::initialize(VulkanInstance& instance, const VulkanDeviceConfig& config) {
    if (initialized_) {
        std::cerr << "[VulkanDeviceManager] Already initialized" << std::endl;
        return false;
    }

    if (!instance.isInitialized()) {
        std::cerr << "[VulkanDeviceManager] VulkanInstance not initialized" << std::endl;
        return false;
    }

    // Enumerate physical devices
    enumeratePhysicalDevices(instance.getInstance(), config.surface);

    if (availableGPUs_.empty()) {
        std::cerr << "[VulkanDeviceManager] No suitable GPUs found" << std::endl;
        return false;
    }

    // Sort GPUs by score
    std::sort(availableGPUs_.begin(), availableGPUs_.end(),
        [this](const GPUInfo& a, const GPUInfo& b) {
            return scoreDevice(a) > scoreDevice(b);
        });

    // Determine which GPUs to use
    std::vector<uint32_t> selectedDevices;

    if (config.enableMultiGPU) {
        // Use all suitable GPUs
        for (const auto& gpu : availableGPUs_) {
            if (gpu.queueFamilies.isComplete()) {
                selectedDevices.push_back(gpu.index);
            }
        }
    } else {
        // Use only the preferred or best GPU
        uint32_t selectedIndex = config.preferredGPUIndex;
        if (selectedIndex >= availableGPUs_.size()) {
            selectedIndex = 0; // Fall back to best GPU
        }
        selectedDevices.push_back(availableGPUs_[selectedIndex].index);
    }

    if (selectedDevices.empty()) {
        std::cerr << "[VulkanDeviceManager] No suitable GPU found" << std::endl;
        return false;
    }

    // Get physical devices from instance
    auto physicalDevices = instance.getInstance().enumeratePhysicalDevices();

    // Create logical devices for selected GPUs
    for (size_t i = 0; i < selectedDevices.size(); ++i) {
        uint32_t gpuIndex = selectedDevices[i];

        // Find GPU info
        auto it = std::find_if(availableGPUs_.begin(), availableGPUs_.end(),
            [gpuIndex](const GPUInfo& info) { return info.index == gpuIndex; });

        if (it == availableGPUs_.end()) {
            continue;
        }

        auto device = std::make_unique<GPUDevice>();
        if (device->initialize(instance.getInstance(), physicalDevices[gpuIndex], *it, config)) {
            if (i == 0) {
                primaryDeviceIndex_ = static_cast<uint32_t>(devices_.size());
            }
            devices_.push_back(std::move(device));
        }
    }

    if (devices_.empty()) {
        std::cerr << "[VulkanDeviceManager] Failed to create any logical device" << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "[VulkanDeviceManager] Initialized with " << devices_.size() << " device(s)" << std::endl;
    return true;
}

void VulkanDeviceManager::shutdown() {
    if (!initialized_) {
        return;
    }

    // Shutdown all devices
    for (auto& device : devices_) {
        device->shutdown();
    }
    devices_.clear();
    availableGPUs_.clear();

    initialized_ = false;
    std::cout << "[VulkanDeviceManager] Shutdown complete" << std::endl;
}

void VulkanDeviceManager::enumeratePhysicalDevices(vk::Instance instance, vk::SurfaceKHR surface) {
    auto physicalDevices = instance.enumeratePhysicalDevices();

    availableGPUs_.clear();
    for (uint32_t i = 0; i < physicalDevices.size(); ++i) {
        auto info = queryDeviceInfo(physicalDevices[i], i, surface);
        availableGPUs_.push_back(info);

        std::cout << "[VulkanDeviceManager] Found GPU " << i << ": " << info.name
                  << " (Score: " << scoreDevice(info) << ")" << std::endl;
    }
}

GPUInfo VulkanDeviceManager::queryDeviceInfo(vk::PhysicalDevice device, uint32_t index, vk::SurfaceKHR surface) {
    GPUInfo info{};
    info.index = index;

    auto properties = device.getProperties();
    info.name = properties.deviceName.data();
    info.type = properties.deviceType;
    info.vendorId = properties.vendorID;
    info.deviceId = properties.deviceID;
    info.apiVersion = properties.apiVersion;
    info.driverVersion = properties.driverVersion;

    // Get memory info
    auto memoryProperties = device.getMemoryProperties();
    for (uint32_t i = 0; i < memoryProperties.memoryHeapCount; ++i) {
        if (memoryProperties.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
            info.totalMemory += memoryProperties.memoryHeaps[i].size;
        }
    }

    // Find queue families
    info.queueFamilies = findQueueFamilies(device, surface);
    info.supportsCompute = info.queueFamilies.hasCompute();
    info.supportsTransfer = info.queueFamilies.hasTransfer();
    info.supportsPresent = info.queueFamilies.presentFamily.has_value();

    return info;
}

QueueFamilyIndices VulkanDeviceManager::findQueueFamilies(vk::PhysicalDevice device, vk::SurfaceKHR surface) {
    QueueFamilyIndices indices;

    auto queueFamilies = device.getQueueFamilyProperties();

    for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
        const auto& family = queueFamilies[i];

        // Graphics queue
        if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphicsFamily = i;
        }

        // Compute queue (prefer dedicated compute queue)
        if ((family.queueFlags & vk::QueueFlagBits::eCompute) &&
            !(family.queueFlags & vk::QueueFlagBits::eGraphics)) {
            indices.computeFamily = i;
        } else if ((family.queueFlags & vk::QueueFlagBits::eCompute) && !indices.computeFamily.has_value()) {
            indices.computeFamily = i;
        }

        // Transfer queue (prefer dedicated transfer queue)
        if ((family.queueFlags & vk::QueueFlagBits::eTransfer) &&
            !(family.queueFlags & vk::QueueFlagBits::eGraphics) &&
            !(family.queueFlags & vk::QueueFlagBits::eCompute)) {
            indices.transferFamily = i;
        } else if ((family.queueFlags & vk::QueueFlagBits::eTransfer) && !indices.transferFamily.has_value()) {
            indices.transferFamily = i;
        }

        // Present queue
        if (surface) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport) {
                indices.presentFamily = i;
            }
        } else {
            // If no surface, assume graphics queue supports present
            if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
                indices.presentFamily = i;
            }
        }

        if (indices.isComplete() && indices.hasCompute() && indices.hasTransfer()) {
            break;
        }
    }

    return indices;
}

int VulkanDeviceManager::scoreDevice(const GPUInfo& info) {
    int score = 0;

    // Prefer discrete GPUs
    if (info.type == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1000;
    } else if (info.type == vk::PhysicalDeviceType::eIntegratedGpu) {
        score += 100;
    }

    // Score based on memory
    score += static_cast<int>(info.totalMemory / (1024 * 1024 * 1024)); // GB of memory

    // Bonus for compute and transfer queues
    if (info.supportsCompute) score += 50;
    if (info.supportsTransfer) score += 50;

    // Must have required capabilities
    if (!info.queueFamilies.isComplete()) {
        score = -1;
    }

    return score;
}

GPUDevice& VulkanDeviceManager::getPrimaryDevice() {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (devices_.empty()) {
        throw std::runtime_error("No GPU devices available");
    }
    return *devices_[primaryDeviceIndex_];
}

const GPUDevice& VulkanDeviceManager::getPrimaryDevice() const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (devices_.empty()) {
        throw std::runtime_error("No GPU devices available");
    }
    return *devices_[primaryDeviceIndex_];
}

GPUDevice* VulkanDeviceManager::getDevice(uint32_t index) {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (index >= devices_.size()) {
        return nullptr;
    }
    return devices_[index].get();
}

const GPUDevice* VulkanDeviceManager::getDevice(uint32_t index) const {
    std::lock_guard<std::mutex> lock(deviceMutex_);
    if (index >= devices_.size()) {
        return nullptr;
    }
    return devices_[index].get();
}

void VulkanDeviceManager::forEachDevice(const std::function<void(GPUDevice&, size_t)>& func) {
    // Use thread pool if available and multiple devices
    if (devices_.size() > 1 && core::ThreadManager::instance().hasPool("gpu")) {
        std::vector<std::future<void>> futures;
        futures.reserve(devices_.size());

        for (size_t i = 0; i < devices_.size(); ++i) {
            auto& device = devices_[i];
            futures.push_back(
                core::ThreadManager::instance().submitTo("gpu", [&func, &device, i]() {
                    func(*device, i);
                })
            );
        }

        // Wait for all tasks
        for (auto& future : futures) {
            future.wait();
        }
    } else {
        // Execute sequentially
        for (size_t i = 0; i < devices_.size(); ++i) {
            func(*devices_[i], i);
        }
    }
}

} // namespace device
