#include "vulkan_device.h"
#include "BS_thread_pool.hpp"
#include "config.h"
#include "thread_manager.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <cstdio>
#include <print>
#include <set>
#include <utility>

namespace device {

// ============================================================================
// GPUDevice Implementation
// ============================================================================

GPUDevice::GPUDevice() = default;

GPUDevice::~GPUDevice() { shutdown(); }

void GPUDevice::shutdown() {
  if (!initialized_) {
    return;
  }

  if (device_) {
    device_->waitIdle();
  }

  // RAII handles cleanup - destroy in reverse order
  device_.reset();
  physicalDevice_.reset();

  initialized_ = false;
  std::println("[GPUDevice] Shutdown: {}", info_.name);
}

GPUDevice::GPUDevice(GPUDevice &&other) noexcept
    : physicalDevice_(std::move(other.physicalDevice_)),
      device_(std::move(other.device_)), info_(std::move(other.info_)),
      graphicsQueue_(other.graphicsQueue_), computeQueue_(other.computeQueue_),
      transferQueue_(other.transferQueue_), presentQueue_(other.presentQueue_),
      initialized_(other.initialized_) {
  other.initialized_ = false;
}

GPUDevice &GPUDevice::operator=(GPUDevice &&other) noexcept {
  if (this != &other) {
    shutdown();
    physicalDevice_ = std::move(other.physicalDevice_);
    device_ = std::move(other.device_);
    info_ = std::move(other.info_);
    graphicsQueue_ = other.graphicsQueue_;
    computeQueue_ = other.computeQueue_;
    transferQueue_ = other.transferQueue_;
    presentQueue_ = other.presentQueue_;
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

void GPUDevice::waitIdle() const {
  if (device_) {
    device_->waitIdle();
  }
}

bool GPUDevice::initialize(const vk::raii::Instance &instance,
                           vk::PhysicalDevice physicalDeviceHandle,
                           const GPUInfo &info) {
  if (initialized_) {
    std::println(stderr, "[GPUDevice] Already initialized");
    return false;
  }

  info_ = info;

  // Create RAII physical device wrapper
  physicalDevice_ = std::make_unique<vk::raii::PhysicalDevice>(
      instance, physicalDeviceHandle);

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
    vk::DeviceQueueCreateInfo queueCreateInfo{{}, family, 1, &queuePriority};
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
  std::vector<const char *> enabledExtensions;
  auto &requiredExtensions =
      core::Config::instance().getVulkanConfig().deviceExtensions;
  for (const auto &ext : requiredExtensions) {
    enabledExtensions.push_back(ext.c_str());
  }

  // Check optional extensions
  auto availableExtensions =
      physicalDevice_->enumerateDeviceExtensionProperties();
  auto &optionalExtensions =
      core::Config::instance().getVulkanConfig().optionalDeviceExtensions;
  for (const auto &optExt : optionalExtensions) {
    auto it =
        std::find_if(availableExtensions.begin(), availableExtensions.end(),
                     [&optExt](const vk::ExtensionProperties &props) {
                       return optExt == props.extensionName.data();
                     });
    if (it != availableExtensions.end()) {
      enabledExtensions.push_back(optExt.c_str());
    }
  }

  // Create logical device
  vk::DeviceCreateInfo createInfo{{},
                                  queueCreateInfos,
                                  {}, // No layers for device
                                  enabledExtensions,
                                  &deviceFeatures};

  try {
    device_ = std::make_unique<vk::raii::Device>(*physicalDevice_, createInfo);
  } catch (const vk::SystemError &e) {
    std::println(stderr, "[GPUDevice] Failed to create logical device: {}",
                 e.what());
    return false;
  }

  // Get queues (raw handles from RAII device)
  if (info.queueFamilies.graphicsFamily.has_value()) {
    graphicsQueue_ =
        device_->getQueue(info.queueFamilies.graphicsFamily.value(), 0);
  }
  if (info.queueFamilies.presentFamily.has_value()) {
    presentQueue_ =
        device_->getQueue(info.queueFamilies.presentFamily.value(), 0);
  }
  if (info.queueFamilies.computeFamily.has_value()) {
    computeQueue_ =
        device_->getQueue(info.queueFamilies.computeFamily.value(), 0);
  }
  if (info.queueFamilies.transferFamily.has_value()) {
    transferQueue_ =
        device_->getQueue(info.queueFamilies.transferFamily.value(), 0);
  }

  initialized_ = true;
  std::println("[GPUDevice] Initialized: {}", info.name);
  return true;
}

// ============================================================================
// DeviceManager Implementation
// ============================================================================

DeviceManager::DeviceManager() = default;

DeviceManager::~DeviceManager() { shutdown(); }

void DeviceManager::shutdown() {
  if (!initialized_) {
    return;
  }

  // Shutdown all devices
  for (auto &device : devices_) {
    device->shutdown();
  }
  devices_.clear();
  availableGPUs_.clear();
  vulkanInstance_ = nullptr;

  initialized_ = false;
  std::println("[DeviceManager] Shutdown complete");
}

bool DeviceManager::initialize(VulkanInstance &instance,
                               const VulkanDeviceConfig &config) {
  if (initialized_) {
    std::println(stderr, "[DeviceManager] Already initialized");
    return false;
  }

  if (!instance.isInitialized()) {
    std::println(stderr, "[DeviceManager] VulkanInstance not initialized");
    return false;
  }

  vulkanInstance_ = &instance;

  // Enumerate physical devices
  enumeratePhysicalDevices(instance.getRaiiInstance(), config.surface);

  if (availableGPUs_.empty()) {
    std::println(stderr, "[DeviceManager] No suitable GPUs found");
    return false;
  }

  // Sort GPUs by score
  std::sort(availableGPUs_.begin(), availableGPUs_.end(),
            [this](const GPUInfo &a, const GPUInfo &b) {
              return scoreDevice(a) > scoreDevice(b);
            });

  // Determine which GPUs to use
  std::vector<uint32_t> selectedDevices;

  if (config.enableMultiGPU) {
    // Use all suitable GPUs
    for (const auto &gpu : availableGPUs_) {
      if (gpu.queueFamilies.hasGraphics()) { // TODO check if only graphics is
                                             // enought, and if it don't need to
                                             // check for present too
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
    std::println(stderr, "[DeviceManager] No suitable GPU found");
    return false;
  }

  if (config.enableMultiGPU) {
    bool onePresent = false;
    for (const uint32_t &i : selectedDevices) {
      if (availableGPUs_[i].queueFamilies.canPresent()) {
        onePresent = true;
      }
    }
    if (!onePresent) {
      std::println(stderr, "[DeviceManager] No GPU has Present Family");
      return false;
    }
  }

  // Get physical devices from instance
  auto physicalDevices = instance.getRaiiInstance().enumeratePhysicalDevices();

  // Create logical devices for selected GPUs
  for (size_t i = 0; i < selectedDevices.size(); ++i) {
    uint32_t gpuIndex = selectedDevices[i];

    // Validate GPU index bounds (device may have been hot-unplugged)
    if (gpuIndex >= physicalDevices.size()) {
      std::println(stderr,
                   "[DeviceManager] GPU index {} out of range (device "
                   "may have been removed)",
                   gpuIndex);
      continue;
    }

    // Find GPU info
    auto it = std::find_if(
        availableGPUs_.begin(), availableGPUs_.end(),
        [gpuIndex](const GPUInfo &info) { return info.index == gpuIndex; });

    if (it == availableGPUs_.end()) {
      continue;
    }

    auto device = std::make_unique<GPUDevice>();
    if (device->initialize(instance.getRaiiInstance(),
                           *physicalDevices[gpuIndex], *it)) {
      if (i == 0) {
        primaryDeviceIndex_ = static_cast<uint32_t>(devices_.size());
      }
      devices_.push_back(std::move(device));
    }
  }

  if (devices_.empty()) {
    std::println(stderr, "[DeviceManager] Failed to create any logical device");
    return false;
  }

  initialized_ = true;
  std::println("[DeviceManager] Initialized with {} device(s)",
               devices_.size());
  return true;
}

void DeviceManager::enumeratePhysicalDevices(const vk::raii::Instance &instance,
                                             vk::SurfaceKHR surface) {
  auto physicalDevices = instance.enumeratePhysicalDevices();

  availableGPUs_.clear();
  for (uint32_t i = 0; i < physicalDevices.size(); ++i) {
    auto info = queryDeviceInfo(*physicalDevices[i], i, surface);
    availableGPUs_.push_back(info);

    std::println("[DeviceManager] Found GPU {}: {} (Score: {})", i, info.name,
                 scoreDevice(info));
  }
}

GPUInfo DeviceManager::queryDeviceInfo(vk::PhysicalDevice device,
                                       uint32_t index, vk::SurfaceKHR surface) {
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
    if (memoryProperties.memoryHeaps[i].flags &
        vk::MemoryHeapFlagBits::eDeviceLocal) {
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

QueueFamilyIndices DeviceManager::findQueueFamilies(vk::PhysicalDevice device,
                                                    vk::SurfaceKHR surface) {
  QueueFamilyIndices indices;

  auto queueFamilies = device.getQueueFamilyProperties();

  for (uint32_t i = 0; i < queueFamilies.size(); ++i) {
    const auto &family = queueFamilies[i];

    // Graphics queue
    if (family.queueFlags & vk::QueueFlagBits::eGraphics) {
      indices.graphicsFamily = i;
    }

    // Compute queue (prefer dedicated compute queue)
    if ((family.queueFlags & vk::QueueFlagBits::eCompute) &&
        !(family.queueFlags & vk::QueueFlagBits::eGraphics)) {
      indices.computeFamily = i;
    } else if ((family.queueFlags & vk::QueueFlagBits::eCompute) &&
               !indices.computeFamily.has_value()) {
      indices.computeFamily = i;
    }

    // Transfer queue (prefer dedicated transfer queue)
    if ((family.queueFlags & vk::QueueFlagBits::eTransfer) &&
        !(family.queueFlags & vk::QueueFlagBits::eGraphics) &&
        !(family.queueFlags & vk::QueueFlagBits::eCompute)) {
      indices.transferFamily = i;
    } else if ((family.queueFlags & vk::QueueFlagBits::eTransfer) &&
               !indices.transferFamily.has_value()) {
      indices.transferFamily = i;
    }

    // Present queue
    if (surface) {
      VkBool32 presentSupport = false;
      presentSupport = device.getSurfaceSupportKHR(i, surface);
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

int DeviceManager::scoreDevice(const GPUInfo &info) {
  int score = 0;

  // Prefer discrete GPUs
  if (info.type == vk::PhysicalDeviceType::eDiscreteGpu) {
    score += 1000;
  } else if (info.type == vk::PhysicalDeviceType::eIntegratedGpu) {
    score += 100;
  }

  // Score based on memory
  score +=
      static_cast<int>(info.totalMemory / (1024 * 1024 * 1024)); // GB of memory

  // Bonus for compute and transfer queues
  if (info.supportsCompute)
    score += 50;
  if (info.supportsTransfer)
    score += 50;

  // Must have required capabilities
  if (!(info.queueFamilies.canPresent() && info.queueFamilies.hasGraphics())) {
    score = -1;
  }

  return score;
}

void DeviceManager::forEachDevice(
    const std::function<void(GPUDevice &, size_t)> &func) {
  // Use thread pool if available and multiple devices
  if (devices_.size() > 1 && core::ThreadManager::instance().hasPool("gpu")) {
    BS::multi_future<void> futures;
    futures.reserve(devices_.size());

    for (size_t i = 0; i < devices_.size(); ++i) {
      auto &device = devices_[i];
      futures.push_back(core::ThreadManager::instance().submitTo(
          "gpu", [&func, &device, i]() { func(*device, i); }));
    }

    // Wait for all tasks
    futures.wait();
  } else {
    std::println(stderr,
                 "[DeviceManager] Theres no \"gpu\" pool in the ThreadManager");
    // Execute sequentially
    for (size_t i = 0; i < devices_.size(); ++i) {
      func(*devices_[i], i);
    }
  }
}

GPUDevice *DeviceManager::getDevice(uint32_t index) {
  std::lock_guard<std::mutex> lock(deviceManagerMutex_);
  if (index >= devices_.size()) {
    return nullptr;
  }
  return devices_[index].get();
}

const GPUDevice *DeviceManager::getDevice(uint32_t index) const {
  std::lock_guard<std::mutex> lock(deviceManagerMutex_);
  if (index >= devices_.size()) {
    return nullptr;
  }
  return devices_[index].get();
}

} // namespace device
