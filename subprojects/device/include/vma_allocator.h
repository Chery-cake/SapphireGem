#ifndef VMA_ALLOCATOR_H_
#define VMA_ALLOCATOR_H_

#include "device_export.h"
#include "vk_mem_alloc_raii.hpp"
#include <memory>
#include <mutex>

namespace device {

// Forward declarations
class GPUDevice;
class DeviceManager;
class VulkanInstance;

/**
 * @brief Wrapper for a VMA-allocated buffer
 *
 * The buffer is automatically destroyed when this object goes out of scope.
 */
struct DEVICE_API AllocatedBuffer {
  std::unique_ptr<vma::raii::Buffer> buffer;
  vk::DeviceSize size = 0;
  std::string name;

  AllocatedBuffer() = default;
  ~AllocatedBuffer() = default;

  // Move only
  AllocatedBuffer(const AllocatedBuffer &) = delete;
  AllocatedBuffer &operator=(const AllocatedBuffer &) = delete;
  AllocatedBuffer(AllocatedBuffer &&) noexcept = default;
  AllocatedBuffer &operator=(AllocatedBuffer &&) noexcept = default;

  [[nodiscard]] bool isValid() const { return buffer != nullptr; }
  [[nodiscard]] vk::Buffer getBuffer() const {
    return buffer ? static_cast<vk::Buffer>(*buffer) : vk::Buffer{};
  }
  [[nodiscard]] vma::Allocation getAllocation() const {
    return buffer ? buffer->getAllocation() : vma::Allocation{};
  }

  // Map/unmap for host-visible buffers
  [[nodiscard]] void *map() const;
  void unmap() const;
  void flush(vk::DeviceSize offset = 0,
             vk::DeviceSize flushSize = VK_WHOLE_SIZE) const;
  void invalidate(vk::DeviceSize offset = 0,
                  vk::DeviceSize invalSize = VK_WHOLE_SIZE) const;
};

/**
 * @brief Wrapper for a VMA-allocated image
 *
 * The image is automatically destroyed when this object goes out of scope.
 */
struct DEVICE_API AllocatedImage {
  std::unique_ptr<vma::raii::Image> image;
  std::unique_ptr<vk::raii::ImageView> view;
  vk::Format format = vk::Format::eUndefined;
  vk::Extent3D extent = {};
  uint32_t mipLevels = 1;
  uint32_t arrayLayers = 1;
  std::string name;

  AllocatedImage() = default;
  ~AllocatedImage() = default;

  // Move only
  AllocatedImage(const AllocatedImage &) = delete;
  AllocatedImage &operator=(const AllocatedImage &) = delete;
  AllocatedImage(AllocatedImage &&) noexcept = default;
  AllocatedImage &operator=(AllocatedImage &&) noexcept = default;

  [[nodiscard]] bool isValid() const { return image != nullptr; }
  [[nodiscard]] vk::Image getImage() const {
    return image ? static_cast<vk::Image>(**image) : vk::Image{};
  }
  [[nodiscard]] vk::ImageView getView() const {
    return view ? static_cast<vk::ImageView>(**view) : vk::ImageView{};
  }
  [[nodiscard]] vma::Allocation getAllocation() const {
    return image ? image->getAllocation() : vma::Allocation{};
  }
};

/**
 * @brief Buffer creation information
 */
struct DEVICE_API BufferCreateInfo {
  vk::DeviceSize size = 0;
  vk::BufferUsageFlags usage;
  vma::MemoryUsage memoryUsage = vma::MemoryUsage::eAuto;
  vma::AllocationCreateFlags flags = {};
  std::string debugName;
};

/**
 * @brief Image creation information
 */
struct DEVICE_API ImageCreateInfo {
  vk::ImageType imageType = vk::ImageType::e2D;
  vk::Format format = vk::Format::eR8G8B8A8Srgb;
  vk::Extent3D extent = {1, 1, 1};
  uint32_t mipLevels = 1;
  uint32_t arrayLayers = 1;
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
  vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
  vk::ImageUsageFlags usage;
  vma::MemoryUsage memoryUsage = vma::MemoryUsage::eAuto;
  vma::AllocationCreateFlags flags;
  std::string debugName;
};

/**
 * @brief Manages GPU memory allocation using VMA
 *
 * Each VMAAllocator is associated with a single GPUDevice.
 * For multi-GPU setups, create one VMAAllocator per device.
 */
class DEVICE_API VMAAllocator {
public:
  VMAAllocator();
  ~VMAAllocator();

  // Disable copy & move
  VMAAllocator(const VMAAllocator &) = delete;
  VMAAllocator &operator=(const VMAAllocator &) = delete;
  VMAAllocator(VMAAllocator &&other) = delete;
  VMAAllocator &operator=(VMAAllocator &&other) = delete;

  /**
   * @brief Initialize the allocator for a device
   * @param instance Vulkan instance
   * @param device GPU device to allocate for
   * @return true if initialization succeeded
   */
  bool initialize(const vk::raii::Instance &instance, GPUDevice &device);

  /**
   * @brief Shutdown and cleanup
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }
  [[nodiscard]] vma::Allocator getAllocator() const;

  // ========== Buffer Operations ==========

  /**
   * @brief Create a buffer with specified parameters
   * @param info Buffer creation info
   * @return Created buffer, or invalid buffer on failure
   */
  AllocatedBuffer createBuffer(const BufferCreateInfo &info);

  /**
   * @brief Create a staging buffer for CPU-to-GPU transfers
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created staging buffer
   */
  AllocatedBuffer createStagingBuffer(vk::DeviceSize size,
                                      const std::string &debugName = "");

  /**
   * @brief Create a vertex buffer
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created vertex buffer
   */
  AllocatedBuffer createVertexBuffer(vk::DeviceSize size,
                                     const std::string &debugName = "");

  /**
   * @brief Create an index buffer
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created index buffer
   */
  AllocatedBuffer createIndexBuffer(vk::DeviceSize size,
                                    const std::string &debugName = "");

  /**
   * @brief Create a uniform buffer
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created uniform buffer
   */
  AllocatedBuffer createUniformBuffer(vk::DeviceSize size,
                                      const std::string &debugName = "");

  /**
   * @brief Create a storage buffer (GPU-only, for transfer-destination use)
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created storage buffer
   */
  AllocatedBuffer createStorageBuffer(vk::DeviceSize size,
                                      const std::string &debugName = "");

  /**
   * @brief Create a host-visible storage buffer (CPU-writable every frame)
   *
   * Uses CpuToGpu memory with persistent mapping so the CPU can write
   * directly without staging. Ideal for per-frame SSBOs that change
   * frequently (e.g. per-face material data).
   *
   * @param size Buffer size
   * @param debugName Optional debug name
   * @return Created storage buffer
   */
  AllocatedBuffer
  createHostVisibleStorageBuffer(vk::DeviceSize size,
                                 const std::string &debugName = "");

  // ========== Image Operations ==========

  /**
   * @brief Create an image with specified parameters
   * @param info Image creation info
   * @return Created image, or invalid image on failure
   */
  AllocatedImage createImage(const ImageCreateInfo &info);

  /**
   * @brief Create a 2D image
   * @param width Image width
   * @param height Image height
   * @param format Image format
   * @param usage Usage flags
   * @param debugName Optional debug name
   * @return Created image
   */
  AllocatedImage createImage2D(uint32_t width, uint32_t height,
                               vk::Format format, vk::ImageUsageFlags usage,
                               const std::string &debugName = "");

  /**
   * @brief Create an image view for an image
   * @param image Image to create view for
   * @param aspectMask Aspect mask for the view
   * @return true if view was created
   */
  bool createImageView(
      AllocatedImage &image,
      vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor);

  // ========== Statistics ==========

  /**
   * @brief Get memory statistics
   * @return Total and used memory statistics
   */
  struct MemoryStats {
    vk::DeviceSize totalAllocated = 0;
    vk::DeviceSize totalUsed = 0;
    uint32_t allocationCount = 0;
  };
  [[nodiscard]] MemoryStats getStats() const;

  /**
   * @brief Set debug name for an allocation (if validation layers enabled)
   * @param buffer Buffer to name
   * @param name Debug name
   */
  void setDebugName(const AllocatedBuffer &buffer, const std::string &name);
  void setDebugName(const AllocatedImage &image, const std::string &name);

private:
  std::unique_ptr<vma::raii::Allocator> allocator_;
  const vk::raii::Device *device_ = nullptr;
  bool initialized_ = false;
  mutable std::mutex allocatorMutex_;
};

/**
 * @brief Manages VMA allocators for all GPU devices
 *
 * Creates and manages one VMAAllocator per active GPU device.
 */
class DEVICE_API VMAManager {
public:
  VMAManager();
  ~VMAManager();

  // Disable copy
  VMAManager(const VMAManager &) = delete;
  VMAManager &operator=(const VMAManager &) = delete;

  /**
   * @brief Initialize allocators for all devices
   * @param instance Vulkan instance
   * @param deviceManager Device manager with initialized devices
   * @return true if initialization succeeded
   */
  bool initialize(const vk::raii::Instance &instance,
                  DeviceManager &deviceManager);

  /**
   * @brief Shutdown and cleanup all allocators
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

  /**
   * @brief Get allocator for the primary device
   * @return Reference to primary allocator
   */
  [[nodiscard]] VMAAllocator &getPrimaryAllocator();
  [[nodiscard]] const VMAAllocator &getPrimaryAllocator() const;

  /**
   * @brief Get allocator for a specific device
   * @param deviceIndex Device index
   * @return Pointer to allocator, or nullptr if not found
   */
  [[nodiscard]] VMAAllocator *getAllocator(uint32_t deviceIndex);
  [[nodiscard]] const VMAAllocator *getAllocator(uint32_t deviceIndex) const;

  /**
   * @brief Get the number of allocators
   * @return Number of allocators (matches device count)
   */
  [[nodiscard]] size_t getAllocatorCount() const { return allocators_.size(); }

private:
  std::vector<std::unique_ptr<VMAAllocator>> allocators_;
  uint32_t primaryIndex_ = 0;
  bool initialized_ = false;
};

} // namespace device

#endif // VMA_ALLOCATOR_H_
