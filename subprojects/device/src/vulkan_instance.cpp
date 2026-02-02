#include "vulkan_instance.h"
#include "config.h"
#include "thread_manager.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// VMA configuration - must match the library build settings
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"
#endif

#include <vk_mem_alloc.h>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace device {

// ============================================================================
// Debug callback for validation layers
// ============================================================================
#ifdef ENGINE_DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallbackVk(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                vk::DebugUtilsMessageTypeFlagsEXT /*messageType*/,
                const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
                void * /*pUserData*/) {
  const char *severityStr = "UNKNOWN";
  if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError) {
    severityStr = "ERROR";
  } else if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
    severityStr = "WARNING";
  } else if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo) {
    severityStr = "INFO";
  } else if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose) {
    severityStr = "VERBOSE";
  }

  fprintf(stderr, "[Vulkan %s] %s\n", severityStr, pCallbackData->pMessage);
  return VK_FALSE;
}
#endif

// ============================================================================
// Buffer Implementation
// ============================================================================

Buffer::Buffer(vk::Buffer buffer, VmaAllocation allocation,
               VmaAllocator allocator, vk::DeviceSize size, BufferUsage usage)
    : buffer_(buffer), allocation_(allocation), allocator_(allocator),
      size_(size), usage_(usage) {}

Buffer::~Buffer() { destroy(); }

Buffer::Buffer(Buffer &&other) noexcept
    : buffer_(other.buffer_), allocation_(other.allocation_),
      allocator_(other.allocator_), size_(other.size_), usage_(other.usage_),
      mappedData_(other.mappedData_) {
  other.buffer_ = nullptr;
  other.allocation_ = nullptr;
  other.allocator_ = nullptr;
  other.size_ = 0;
  other.mappedData_ = nullptr;
}

Buffer &Buffer::operator=(Buffer &&other) noexcept {
  if (this != &other) {
    destroy();
    buffer_ = other.buffer_;
    allocation_ = other.allocation_;
    allocator_ = other.allocator_;
    size_ = other.size_;
    usage_ = other.usage_;
    mappedData_ = other.mappedData_;

    other.buffer_ = nullptr;
    other.allocation_ = nullptr;
    other.allocator_ = nullptr;
    other.size_ = 0;
    other.mappedData_ = nullptr;
  }
  return *this;
}

void Buffer::destroy() {
  if (mappedData_ && allocator_ && allocation_) {
    vmaUnmapMemory(allocator_, allocation_);
    mappedData_ = nullptr;
  }
  if (buffer_ && allocator_ && allocation_) {
    vmaDestroyBuffer(allocator_, static_cast<VkBuffer>(buffer_), allocation_);
  }
  buffer_ = nullptr;
  allocation_ = nullptr;
  allocator_ = nullptr;
  size_ = 0;
}

void *Buffer::map() {
  if (!allocator_ || !allocation_) {
    return nullptr;
  }
  if (mappedData_) {
    return mappedData_;
  }
  if (vmaMapMemory(allocator_, allocation_, &mappedData_) != VK_SUCCESS) {
    mappedData_ = nullptr;
  }
  return mappedData_;
}

void Buffer::unmap() {
  if (mappedData_ && allocator_ && allocation_) {
    vmaUnmapMemory(allocator_, allocation_);
    mappedData_ = nullptr;
  }
}

void Buffer::flush(vk::DeviceSize offset, vk::DeviceSize size) {
  if (allocator_ && allocation_) {
    vmaFlushAllocation(allocator_, allocation_, offset, size);
  }
}

void Buffer::invalidate(vk::DeviceSize offset, vk::DeviceSize size) {
  if (allocator_ && allocation_) {
    vmaInvalidateAllocation(allocator_, allocation_, offset, size);
  }
}

// ============================================================================
// GPUManager Implementation
// ============================================================================

GPUManager::~GPUManager() { shutdown(); }

GPUManager::GPUManager(GPUManager &&other) noexcept
    : availableGPUs_(std::move(other.availableGPUs_)),
      devices_(std::move(other.devices_)),
      initialized_(other.initialized_) {
  other.initialized_ = false;
}

GPUManager &GPUManager::operator=(GPUManager &&other) noexcept {
  if (this != &other) {
    shutdown();
    availableGPUs_ = std::move(other.availableGPUs_);
    devices_ = std::move(other.devices_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

void GPUManager::findQueueFamilies(GPUInfo &gpuInfo) {
  for (uint32_t i = 0; i < gpuInfo.queueFamilies.size(); ++i) {
    const auto &queueFamily = gpuInfo.queueFamilies[i];

    // Graphics queue (prefer dedicated)
    if ((queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
        gpuInfo.graphicsQueueFamily == UINT32_MAX) {
      gpuInfo.graphicsQueueFamily = i;
    }

    // Compute queue (prefer dedicated, not graphics)
    if ((queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
        !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
        gpuInfo.computeQueueFamily == UINT32_MAX) {
      gpuInfo.computeQueueFamily = i;
    }

    // Transfer queue (prefer dedicated, not graphics or compute)
    if ((queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) &&
        !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) &&
        !(queueFamily.queueFlags & vk::QueueFlagBits::eCompute) &&
        gpuInfo.transferQueueFamily == UINT32_MAX) {
      gpuInfo.transferQueueFamily = i;
    }
  }

  // Fallback: if no dedicated compute queue, use graphics queue
  if (gpuInfo.computeQueueFamily == UINT32_MAX &&
      gpuInfo.graphicsQueueFamily != UINT32_MAX) {
    for (uint32_t i = 0; i < gpuInfo.queueFamilies.size(); ++i) {
      if (gpuInfo.queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute) {
        gpuInfo.computeQueueFamily = i;
        break;
      }
    }
  }

  // Fallback: if no dedicated transfer queue, use graphics queue
  if (gpuInfo.transferQueueFamily == UINT32_MAX &&
      gpuInfo.graphicsQueueFamily != UINT32_MAX) {
    gpuInfo.transferQueueFamily = gpuInfo.graphicsQueueFamily;
  }
}

bool GPUManager::initialize(
    const vk::raii::Instance &instance,
    const std::vector<std::string> &deviceExtensions) {
  if (initialized_) {
    return true;
  }

  // Enumerate physical devices
  auto physicalDevices = instance.enumeratePhysicalDevices();
  if (physicalDevices.empty()) {
    fprintf(stderr, "[GPUManager] No Vulkan-capable GPUs found!\n");
    return false;
  }

  // Gather GPU information
  availableGPUs_.reserve(physicalDevices.size());
  for (auto &physDevice : physicalDevices) {
    GPUInfo info;
    info.physicalDevice = *physDevice;
    info.properties = physDevice.getProperties();
    info.memoryProperties = physDevice.getMemoryProperties();
    info.features = physDevice.getFeatures();
    info.queueFamilies = physDevice.getQueueFamilyProperties();
    findQueueFamilies(info);

    availableGPUs_.push_back(std::move(info));
  }

  // Sort GPUs: discrete first, then integrated, then others
  std::sort(availableGPUs_.begin(), availableGPUs_.end(),
            [](const GPUInfo &a, const GPUInfo &b) {
              if (a.isDiscrete() != b.isDiscrete()) {
                return a.isDiscrete();
              }
              if (a.isIntegrated() != b.isIntegrated()) {
                return a.isIntegrated();
              }
              return false;
            });

  // Get configuration
  auto &config = core::Config::instance();
  auto gpuConfig = config.getGPUConfig();

  // Determine how many GPUs to initialize
  uint32_t gpuCount = gpuConfig.enableMultiGPU
                          ? std::min(gpuConfig.gpuCount,
                                     static_cast<uint32_t>(availableGPUs_.size()))
                          : 1;

  // Create logical devices
  for (uint32_t i = 0; i < gpuCount; ++i) {
    uint32_t gpuIndex =
        (i == 0 && gpuConfig.preferredGPUIndex < availableGPUs_.size())
            ? gpuConfig.preferredGPUIndex
            : i;

    if (gpuIndex >= availableGPUs_.size()) {
      continue;
    }

    if (createLogicalDevice(instance, availableGPUs_[gpuIndex],
                            deviceExtensions)) {
      devices_.back().gpuIndex = gpuIndex;
      printf("[GPUManager] Initialized GPU %u: %s\n", gpuIndex,
             availableGPUs_[gpuIndex].getName().c_str());
    }
  }

  if (devices_.empty()) {
    fprintf(stderr, "[GPUManager] Failed to create any logical devices!\n");
    return false;
  }

  // Create GPU thread pools using ThreadManager
  auto &threadManager = core::ThreadManager::instance();
  auto threadAlloc = config.getEffectiveThreadAllocation();

  for (uint32_t i = 0; i < devices_.size(); ++i) {
    std::string poolName = "gpu_" + std::to_string(i);
    uint32_t threadsPerGPU =
        threadAlloc.gpuThreads > 0 ? threadAlloc.gpuThreads : 1;

    if (!threadManager.hasPool(poolName)) {
      core::ThreadPoolConfig poolConfig;
      poolConfig.name = poolName;
      poolConfig.type = core::PoolType::GPU;
      poolConfig.threadCount = threadsPerGPU;
      threadManager.createPool(poolConfig);
    }
  }

  initialized_ = true;
  return true;
}

bool GPUManager::createLogicalDevice(
    const vk::raii::Instance &instance, const GPUInfo &gpuInfo,
    const std::vector<std::string> &deviceExtensions) {

  if (!gpuInfo.supportsGraphics()) {
    fprintf(stderr, "[GPUManager] GPU %s does not support graphics!\n",
            gpuInfo.getName().c_str());
    return false;
  }

  // Prepare queue create infos
  std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
  std::vector<float> queuePriorities = {1.0f};

  // Track which queue families we're creating
  std::vector<uint32_t> uniqueQueueFamilies;

  if (gpuInfo.graphicsQueueFamily != UINT32_MAX) {
    uniqueQueueFamilies.push_back(gpuInfo.graphicsQueueFamily);
  }
  if (gpuInfo.computeQueueFamily != UINT32_MAX &&
      gpuInfo.computeQueueFamily != gpuInfo.graphicsQueueFamily) {
    uniqueQueueFamilies.push_back(gpuInfo.computeQueueFamily);
  }
  if (gpuInfo.transferQueueFamily != UINT32_MAX &&
      gpuInfo.transferQueueFamily != gpuInfo.graphicsQueueFamily &&
      gpuInfo.transferQueueFamily != gpuInfo.computeQueueFamily) {
    uniqueQueueFamilies.push_back(gpuInfo.transferQueueFamily);
  }

  for (uint32_t queueFamily : uniqueQueueFamilies) {
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = queuePriorities.data();
    queueCreateInfos.push_back(queueCreateInfo);
  }

  // Convert extension names to C strings
  std::vector<const char *> extensionPtrs;
  for (const auto &ext : deviceExtensions) {
    extensionPtrs.push_back(ext.c_str());
  }

  // Device features
  vk::PhysicalDeviceFeatures deviceFeatures;
  // Enable commonly used features
  deviceFeatures.samplerAnisotropy = gpuInfo.features.samplerAnisotropy;
  deviceFeatures.fillModeNonSolid = gpuInfo.features.fillModeNonSolid;
  deviceFeatures.wideLines = gpuInfo.features.wideLines;

  // Create device
  vk::DeviceCreateInfo deviceCreateInfo;
  deviceCreateInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
  deviceCreateInfo.enabledExtensionCount =
      static_cast<uint32_t>(extensionPtrs.size());
  deviceCreateInfo.ppEnabledExtensionNames = extensionPtrs.data();
  deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

  try {
    // Create physical device wrapper for RAII
    vk::raii::PhysicalDevice physDevice(instance, gpuInfo.physicalDevice);

    LogicalDevice logicalDevice;
    logicalDevice.device =
        std::make_unique<vk::raii::Device>(physDevice, deviceCreateInfo);

    // Get queues
    if (gpuInfo.graphicsQueueFamily != UINT32_MAX) {
      logicalDevice.graphicsQueue = std::make_unique<vk::raii::Queue>(
          logicalDevice.device->getQueue(gpuInfo.graphicsQueueFamily, 0));
    }
    if (gpuInfo.computeQueueFamily != UINT32_MAX) {
      logicalDevice.computeQueue = std::make_unique<vk::raii::Queue>(
          logicalDevice.device->getQueue(gpuInfo.computeQueueFamily, 0));
    }
    if (gpuInfo.transferQueueFamily != UINT32_MAX) {
      logicalDevice.transferQueue = std::make_unique<vk::raii::Queue>(
          logicalDevice.device->getQueue(gpuInfo.transferQueueFamily, 0));
    }

    // Create VMA allocator
    if (!createVMAAllocator(logicalDevice, instance, gpuInfo)) {
      fprintf(stderr, "[GPUManager] Failed to create VMA allocator for %s\n",
              gpuInfo.getName().c_str());
      return false;
    }

    devices_.push_back(std::move(logicalDevice));
    return true;

  } catch (const vk::SystemError &e) {
    fprintf(stderr, "[GPUManager] Failed to create logical device: %s\n",
            e.what());
    return false;
  }
}

bool GPUManager::createVMAAllocator(LogicalDevice &device,
                                    const vk::raii::Instance &instance,
                                    const GPUInfo &gpuInfo) {
  // VMA requires Vulkan function pointers when using dynamic loading
  VmaVulkanFunctions vulkanFunctions = {};
  vulkanFunctions.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
  vulkanFunctions.vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
  allocatorInfo.physicalDevice =
      static_cast<VkPhysicalDevice>(gpuInfo.physicalDevice);
  allocatorInfo.device = static_cast<VkDevice>(**device.device);
  allocatorInfo.instance = static_cast<VkInstance>(*instance);
  allocatorInfo.pVulkanFunctions = &vulkanFunctions;

  VkResult result = vmaCreateAllocator(&allocatorInfo, &device.allocator);
  return result == VK_SUCCESS;
}

void GPUManager::shutdown() {
  if (!initialized_) {
    return;
  }

  // Destroy GPU thread pools
  auto &threadManager = core::ThreadManager::instance();
  for (uint32_t i = 0; i < devices_.size(); ++i) {
    std::string poolName = "gpu_" + std::to_string(i);
    threadManager.destroyPool(poolName);
  }

  // Destroy VMA allocators and devices
  for (auto &device : devices_) {
    if (device.allocator) {
      vmaDestroyAllocator(device.allocator);
      device.allocator = nullptr;
    }
    // RAII handles device destruction
  }

  devices_.clear();
  availableGPUs_.clear();
  initialized_ = false;
}

LogicalDevice *GPUManager::getPrimaryDevice() {
  return devices_.empty() ? nullptr : &devices_[0];
}

LogicalDevice *GPUManager::getDevice(uint32_t index) {
  return index < devices_.size() ? &devices_[index] : nullptr;
}

Buffer GPUManager::createBuffer(uint32_t deviceIndex, vk::DeviceSize size,
                                vk::BufferUsageFlags bufferUsage,
                                BufferUsage memoryUsage) {
  if (deviceIndex >= devices_.size()) {
    return Buffer();
  }

  auto &device = devices_[deviceIndex];
  if (!device.allocator) {
    return Buffer();
  }

  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = static_cast<VkBufferUsageFlags>(bufferUsage);
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocInfo = {};
  switch (memoryUsage) {
  case BufferUsage::GPU_ONLY:
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    break;
  case BufferUsage::CPU_TO_GPU:
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    break;
  case BufferUsage::GPU_TO_CPU:
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    break;
  case BufferUsage::CPU_ONLY:
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    break;
  }

  VkBuffer buffer;
  VmaAllocation allocation;
  VkResult result = vmaCreateBuffer(device.allocator, &bufferInfo, &allocInfo,
                                    &buffer, &allocation, nullptr);

  if (result != VK_SUCCESS) {
    return Buffer();
  }

  return Buffer(vk::Buffer(buffer), allocation, device.allocator, size,
                memoryUsage);
}

Buffer GPUManager::createBuffer(vk::DeviceSize size,
                                vk::BufferUsageFlags bufferUsage,
                                BufferUsage memoryUsage) {
  return createBuffer(0, size, bufferUsage, memoryUsage);
}

void GPUManager::submitWork(uint32_t deviceIndex,
                            std::function<void()> work) {
  if (deviceIndex >= devices_.size()) {
    return;
  }

  auto &threadManager = core::ThreadManager::instance();
  std::string poolName = "gpu_" + std::to_string(deviceIndex);

  if (threadManager.hasPool(poolName)) {
    threadManager.submitTo(poolName, std::move(work));
  } else {
    // Fallback: execute synchronously
    work();
  }
}

// ============================================================================
// VulkanInstance Implementation
// ============================================================================

VulkanInstance::VulkanInstance() = default;

VulkanInstance::~VulkanInstance() { shutdown(); }

VulkanInstance::VulkanInstance(VulkanInstance &&other) noexcept
    : context_(std::move(other.context_)),
      instance_(std::move(other.instance_)),
      debugMessenger_(std::move(other.debugMessenger_)),
      gpuManager_(std::move(other.gpuManager_)),
      deviceExtensions_(std::move(other.deviceExtensions_)),
      initialized_(other.initialized_) {
  other.initialized_ = false;
}

VulkanInstance &VulkanInstance::operator=(VulkanInstance &&other) noexcept {
  if (this != &other) {
    shutdown();
    context_ = std::move(other.context_);
    instance_ = std::move(other.instance_);
    debugMessenger_ = std::move(other.debugMessenger_);
    gpuManager_ = std::move(other.gpuManager_);
    deviceExtensions_ = std::move(other.deviceExtensions_);
    initialized_ = other.initialized_;
    other.initialized_ = false;
  }
  return *this;
}

bool VulkanInstance::initialize(const VulkanRequirements &requirements) {
  if (initialized_) {
    return true;
  }

  try {
    // Create context and load base Vulkan functions
    context_ = std::make_unique<vk::raii::Context>();

    // Initialize the dynamic dispatcher with vkGetInstanceProcAddr
    VULKAN_HPP_DEFAULT_DISPATCHER.init(context_->getDispatcher()->vkGetInstanceProcAddr);

    // Get configuration
    auto &config = core::Config::instance();
    auto vulkanConfig = config.getVulkanConfig();
    auto appConfig = config.getApplicationConfig();

    // Merge required extensions with config extensions
    std::vector<std::string> extensions = requirements.requiredExtensions;
    for (const auto &ext : vulkanConfig.instanceExtensions) {
      if (std::find(extensions.begin(), extensions.end(), ext) ==
          extensions.end()) {
        extensions.push_back(ext);
      }
    }

    // Merge required layers with config layers
    std::vector<std::string> layers = requirements.requiredLayers;
    for (const auto &layer : vulkanConfig.instanceLayers) {
      if (std::find(layers.begin(), layers.end(), layer) == layers.end()) {
        layers.push_back(layer);
      }
    }

#ifdef ENGINE_DEBUG
    // Add debug extension for validation
    if (std::find(extensions.begin(), extensions.end(),
                  VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == extensions.end()) {
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
#endif

    // Check extension support
    auto unsupportedExt = checkExtensionSupport(extensions);
    if (!unsupportedExt.empty()) {
      fprintf(stderr,
              "[VulkanInstance] Unsupported instance extensions:\n");
      for (const auto &ext : unsupportedExt) {
        fprintf(stderr, "  - %s\n", ext.c_str());
      }
      // Remove unsupported extensions
      for (const auto &ext : unsupportedExt) {
        extensions.erase(
            std::remove(extensions.begin(), extensions.end(), ext),
            extensions.end());
      }
    }

    // Check layer support
    auto unsupportedLayers = checkLayerSupport(layers);
    if (!unsupportedLayers.empty()) {
      fprintf(stderr, "[VulkanInstance] Unsupported instance layers:\n");
      for (const auto &layer : unsupportedLayers) {
        fprintf(stderr, "  - %s\n", layer.c_str());
      }
      // Remove unsupported layers
      for (const auto &layer : unsupportedLayers) {
        layers.erase(std::remove(layers.begin(), layers.end(), layer),
                     layers.end());
      }
    }

    // Convert to C strings
    std::vector<const char *> extensionPtrs;
    for (const auto &ext : extensions) {
      extensionPtrs.push_back(ext.c_str());
    }

    std::vector<const char *> layerPtrs;
    for (const auto &layer : layers) {
      layerPtrs.push_back(layer.c_str());
    }

    // Application info
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = appConfig.applicationName.c_str();
    appInfo.applicationVersion = appConfig.applicationVersion;
    appInfo.pEngineName = vulkanConfig.engineName.c_str();
    appInfo.engineVersion = vulkanConfig.engineVersion;
    appInfo.apiVersion = vulkanConfig.minApiVersion;

    // Instance create info
    vk::InstanceCreateInfo createInfo;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(extensionPtrs.size());
    createInfo.ppEnabledExtensionNames = extensionPtrs.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layerPtrs.size());
    createInfo.ppEnabledLayerNames = layerPtrs.data();

    // Create instance
    instance_ = std::make_unique<vk::raii::Instance>(*context_, createInfo);

    // Initialize dispatcher with instance - use raw VkInstance
    VULKAN_HPP_DEFAULT_DISPATCHER.init(static_cast<VkInstance>(**instance_),
                                        context_->getDispatcher()->vkGetInstanceProcAddr);

    // Setup debug messenger
#ifdef ENGINE_DEBUG
    setupDebugMessenger();
#endif

    // Store device extensions from config for GPU initialization
    deviceExtensions_ = vulkanConfig.deviceExtensions;

    // Initialize GPU manager
    if (!gpuManager_.initialize(*instance_, deviceExtensions_)) {
      fprintf(stderr, "[VulkanInstance] Failed to initialize GPU manager!\n");
      return false;
    }

    initialized_ = true;
    printf("[VulkanInstance] Initialized successfully with %u GPU(s)\n",
           gpuManager_.getDeviceCount());
    return true;

  } catch (const vk::SystemError &e) {
    fprintf(stderr, "[VulkanInstance] Vulkan error: %s\n", e.what());
    return false;
  } catch (const std::exception &e) {
    fprintf(stderr, "[VulkanInstance] Error: %s\n", e.what());
    return false;
  }
}

bool VulkanInstance::initializeFromConfig() {
  VulkanRequirements requirements;
  // Config extensions/layers are already merged in initialize()
  return initialize(requirements);
}

void VulkanInstance::shutdown() {
  if (!initialized_) {
    return;
  }

  // Shutdown GPU manager first
  gpuManager_.shutdown();

  // Clear debug messenger
  debugMessenger_.reset();

  // Clear instance
  instance_.reset();

  // Clear context
  context_.reset();

  deviceExtensions_.clear();
  initialized_ = false;
}

bool VulkanInstance::setupDebugMessenger() {
#ifdef ENGINE_DEBUG
  try {
    vk::DebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.messageSeverity =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning;
    createInfo.messageType =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = debugCallbackVk;

    debugMessenger_ =
        std::make_unique<vk::raii::DebugUtilsMessengerEXT>(*instance_, createInfo);
    return true;
  } catch (const vk::SystemError &e) {
    fprintf(stderr, "[VulkanInstance] Failed to setup debug messenger: %s\n",
            e.what());
    return false;
  }
#else
  return true;
#endif
}

std::vector<std::string> VulkanInstance::getAvailableExtensions() {
  std::vector<std::string> result;
  try {
    auto extensions = vk::enumerateInstanceExtensionProperties();
    result.reserve(extensions.size());
    for (const auto &ext : extensions) {
      result.emplace_back(ext.extensionName.data());
    }
  } catch (const vk::SystemError &) {
    // Return empty on error
  }
  return result;
}

std::vector<std::string> VulkanInstance::getAvailableLayers() {
  std::vector<std::string> result;
  try {
    auto layers = vk::enumerateInstanceLayerProperties();
    result.reserve(layers.size());
    for (const auto &layer : layers) {
      result.emplace_back(layer.layerName.data());
    }
  } catch (const vk::SystemError &) {
    // Return empty on error
  }
  return result;
}

std::vector<std::string>
VulkanInstance::checkExtensionSupport(const std::vector<std::string> &extensions) {
  std::vector<std::string> unsupported;
  auto available = getAvailableExtensions();

  for (const auto &ext : extensions) {
    if (std::find(available.begin(), available.end(), ext) == available.end()) {
      unsupported.push_back(ext);
    }
  }

  return unsupported;
}

std::vector<std::string>
VulkanInstance::checkLayerSupport(const std::vector<std::string> &layers) {
  std::vector<std::string> unsupported;
  auto available = getAvailableLayers();

  for (const auto &layer : layers) {
    if (std::find(available.begin(), available.end(), layer) ==
        available.end()) {
      unsupported.push_back(layer);
    }
  }

  return unsupported;
}

} // namespace device
