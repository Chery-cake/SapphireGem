#include "vma_allocator.h"
#include <iostream>

namespace device {

// ============================================================================
// AllocatedBuffer Implementation
// ============================================================================

void* AllocatedBuffer::map(vma::Allocator allocator) {
    return allocator.mapMemory(allocation);
}

void AllocatedBuffer::unmap(vma::Allocator allocator) {
    allocator.unmapMemory(allocation);
}

void AllocatedBuffer::flush(vma::Allocator allocator, vk::DeviceSize offset, vk::DeviceSize flushSize) {
    allocator.flushAllocation(allocation, offset, flushSize);
}

void AllocatedBuffer::invalidate(vma::Allocator allocator, vk::DeviceSize offset, vk::DeviceSize invalSize) {
    allocator.invalidateAllocation(allocation, offset, invalSize);
}

// ============================================================================
// VMAAllocator Implementation
// ============================================================================

VMAAllocator::VMAAllocator() = default;

VMAAllocator::~VMAAllocator() {
    shutdown();
}

VMAAllocator::VMAAllocator(VMAAllocator&& other) noexcept
    : allocator_(other.allocator_)
    , device_(other.device_)
    , initialized_(other.initialized_) {
    other.allocator_ = nullptr;
    other.device_ = nullptr;
    other.initialized_ = false;
}

VMAAllocator& VMAAllocator::operator=(VMAAllocator&& other) noexcept {
    if (this != &other) {
        shutdown();
        allocator_ = other.allocator_;
        device_ = other.device_;
        initialized_ = other.initialized_;
        other.allocator_ = nullptr;
        other.device_ = nullptr;
        other.initialized_ = false;
    }
    return *this;
}

bool VMAAllocator::initialize(vk::Instance instance, GPUDevice& device) {
    if (initialized_) {
        std::cerr << "[VMAAllocator] Already initialized" << std::endl;
        return false;
    }

    device_ = device.getDevice();

    // Setup VMA allocator create info using Vulkan-Hpp dynamic dispatch
    vma::AllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = device.getPhysicalDevice();
    allocatorInfo.device = device_;

    // Use dynamic function dispatch
    allocatorInfo.flags = vma::AllocatorCreateFlagBits::eExtMemoryBudget;

    // Create the Vulkan functions struct for dynamic dispatch
    vma::VulkanFunctions vulkanFunctions{};
    vulkanFunctions.vkGetInstanceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    try {
        allocator_ = vma::createAllocator(allocatorInfo);
    } catch (const vk::SystemError& e) {
        std::cerr << "[VMAAllocator] Failed to create allocator: " << e.what() << std::endl;
        return false;
    }

    initialized_ = true;
    std::cout << "[VMAAllocator] Initialized for device: " << device.getInfo().name << std::endl;
    return true;
}

void VMAAllocator::shutdown() {
    if (!initialized_) {
        return;
    }

    if (allocator_) {
        allocator_.destroy();
        allocator_ = nullptr;
    }

    device_ = nullptr;
    initialized_ = false;
    std::cout << "[VMAAllocator] Shutdown complete" << std::endl;
}

AllocatedBuffer VMAAllocator::createBuffer(const BufferCreateInfo& info) {
    std::lock_guard<std::mutex> lock(allocatorMutex_);

    AllocatedBuffer result{};
    result.size = info.size;
    result.name = info.debugName;

    vk::BufferCreateInfo bufferInfo{
        {},
        info.size,
        info.usage,
        vk::SharingMode::eExclusive
    };

    vma::AllocationCreateInfo allocInfo{};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.flags;

    try {
        auto [buffer, allocation] = allocator_.createBuffer(bufferInfo, allocInfo);
        result.buffer = buffer;
        result.allocation = allocation;
        result.allocationInfo = allocator_.getAllocationInfo(allocation);
    } catch (const vk::SystemError& e) {
        std::cerr << "[VMAAllocator] Failed to create buffer: " << e.what() << std::endl;
        return result;
    }

    return result;
}

AllocatedBuffer VMAAllocator::createStagingBuffer(vk::DeviceSize size, const std::string& debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eTransferSrc;
    info.memoryUsage = vma::MemoryUsage::eCpuOnly;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "StagingBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createVertexBuffer(vk::DeviceSize size, const std::string& debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "VertexBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createIndexBuffer(vk::DeviceSize size, const std::string& debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "IndexBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createUniformBuffer(vk::DeviceSize size, const std::string& debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    info.memoryUsage = vma::MemoryUsage::eCpuToGpu;
    info.flags = vma::AllocationCreateFlagBits::eMapped;
    info.debugName = debugName.empty() ? "UniformBuffer" : debugName;
    return createBuffer(info);
}

AllocatedBuffer VMAAllocator::createStorageBuffer(vk::DeviceSize size, const std::string& debugName) {
    BufferCreateInfo info{};
    info.size = size;
    info.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "StorageBuffer" : debugName;
    return createBuffer(info);
}

void VMAAllocator::destroyBuffer(AllocatedBuffer& buffer) {
    std::lock_guard<std::mutex> lock(allocatorMutex_);

    if (buffer.isValid()) {
        allocator_.destroyBuffer(buffer.buffer, buffer.allocation);
        buffer.buffer = nullptr;
        buffer.allocation = nullptr;
        buffer.size = 0;
    }
}

AllocatedImage VMAAllocator::createImage(const ImageCreateInfo& info) {
    std::lock_guard<std::mutex> lock(allocatorMutex_);

    AllocatedImage result{};
    result.format = info.format;
    result.extent = info.extent;
    result.mipLevels = info.mipLevels;
    result.arrayLayers = info.arrayLayers;
    result.name = info.debugName;

    vk::ImageCreateInfo imageInfo{
        {},
        info.imageType,
        info.format,
        info.extent,
        info.mipLevels,
        info.arrayLayers,
        info.samples,
        info.tiling,
        info.usage,
        vk::SharingMode::eExclusive
    };

    vma::AllocationCreateInfo allocInfo{};
    allocInfo.usage = info.memoryUsage;
    allocInfo.flags = info.flags;

    try {
        auto [image, allocation] = allocator_.createImage(imageInfo, allocInfo);
        result.image = image;
        result.allocation = allocation;
        result.allocationInfo = allocator_.getAllocationInfo(allocation);
    } catch (const vk::SystemError& e) {
        std::cerr << "[VMAAllocator] Failed to create image: " << e.what() << std::endl;
        return result;
    }

    return result;
}

AllocatedImage VMAAllocator::createImage2D(uint32_t width, uint32_t height,
                                            vk::Format format, vk::ImageUsageFlags usage,
                                            const std::string& debugName) {
    ImageCreateInfo info{};
    info.imageType = vk::ImageType::e2D;
    info.format = format;
    info.extent = vk::Extent3D{width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = vk::SampleCountFlagBits::e1;
    info.tiling = vk::ImageTiling::eOptimal;
    info.usage = usage;
    info.memoryUsage = vma::MemoryUsage::eGpuOnly;
    info.debugName = debugName.empty() ? "Image2D" : debugName;
    return createImage(info);
}

bool VMAAllocator::createImageView(AllocatedImage& image, vk::ImageAspectFlags aspectMask) {
    if (!image.isValid() || !device_) {
        return false;
    }

    vk::ImageViewType viewType = vk::ImageViewType::e2D;
    if (image.arrayLayers > 1) {
        viewType = vk::ImageViewType::e2DArray;
    }

    vk::ImageViewCreateInfo viewInfo{
        {},
        image.image,
        viewType,
        image.format,
        {},  // Component mapping
        vk::ImageSubresourceRange{
            aspectMask,
            0, image.mipLevels,
            0, image.arrayLayers
        }
    };

    try {
        image.view = device_.createImageView(viewInfo);
        return true;
    } catch (const vk::SystemError& e) {
        std::cerr << "[VMAAllocator] Failed to create image view: " << e.what() << std::endl;
        return false;
    }
}

void VMAAllocator::destroyImage(AllocatedImage& image) {
    std::lock_guard<std::mutex> lock(allocatorMutex_);

    if (image.view && device_) {
        device_.destroyImageView(image.view);
        image.view = nullptr;
    }

    if (image.isValid()) {
        allocator_.destroyImage(image.image, image.allocation);
        image.image = nullptr;
        image.allocation = nullptr;
    }
}

VMAAllocator::MemoryStats VMAAllocator::getStats() const {
    std::lock_guard<std::mutex> lock(allocatorMutex_);

    MemoryStats stats{};
    if (!initialized_) {
        return stats;
    }

    auto budget = allocator_.getHeapBudgets();
    for (const auto& heap : budget) {
        stats.totalAllocated += heap.statistics.blockBytes;
        stats.totalUsed += heap.statistics.allocationBytes;
        stats.allocationCount += heap.statistics.allocationCount;
    }

    return stats;
}

void VMAAllocator::setDebugName(const AllocatedBuffer& buffer, const std::string& name) {
    if (!initialized_ || !buffer.isValid()) {
        return;
    }
    allocator_.setAllocationName(buffer.allocation, name.c_str());
}

void VMAAllocator::setDebugName(const AllocatedImage& image, const std::string& name) {
    if (!initialized_ || !image.isValid()) {
        return;
    }
    allocator_.setAllocationName(image.allocation, name.c_str());
}

// ============================================================================
// VMAManager Implementation
// ============================================================================

VMAManager::VMAManager() = default;

VMAManager::~VMAManager() {
    shutdown();
}

bool VMAManager::initialize(vk::Instance instance, VulkanDeviceManager& deviceManager) {
    if (initialized_) {
        std::cerr << "[VMAManager] Already initialized" << std::endl;
        return false;
    }

    const auto& devices = deviceManager.getDevices();
    allocators_.reserve(devices.size());

    for (size_t i = 0; i < devices.size(); ++i) {
        auto allocator = std::make_unique<VMAAllocator>();
        if (!allocator->initialize(instance, *devices[i])) {
            std::cerr << "[VMAManager] Failed to create allocator for device " << i << std::endl;
            shutdown();
            return false;
        }
        allocators_.push_back(std::move(allocator));
    }

    // Primary allocator matches primary device
    primaryIndex_ = 0;

    initialized_ = true;
    std::cout << "[VMAManager] Initialized with " << allocators_.size() << " allocator(s)" << std::endl;
    return true;
}

void VMAManager::shutdown() {
    if (!initialized_) {
        return;
    }

    for (auto& allocator : allocators_) {
        allocator->shutdown();
    }
    allocators_.clear();

    initialized_ = false;
    std::cout << "[VMAManager] Shutdown complete" << std::endl;
}

VMAAllocator& VMAManager::getPrimaryAllocator() {
    if (allocators_.empty()) {
        throw std::runtime_error("No allocators available");
    }
    return *allocators_[primaryIndex_];
}

const VMAAllocator& VMAManager::getPrimaryAllocator() const {
    if (allocators_.empty()) {
        throw std::runtime_error("No allocators available");
    }
    return *allocators_[primaryIndex_];
}

VMAAllocator* VMAManager::getAllocator(uint32_t deviceIndex) {
    if (deviceIndex >= allocators_.size()) {
        return nullptr;
    }
    return allocators_[deviceIndex].get();
}

const VMAAllocator* VMAManager::getAllocator(uint32_t deviceIndex) const {
    if (deviceIndex >= allocators_.size()) {
        return nullptr;
    }
    return allocators_[deviceIndex].get();
}

} // namespace device
