#ifndef IMAGE_ARRAY_REGISTRY_H_
#define IMAGE_ARRAY_REGISTRY_H_

#include "bindless_types.h"
#include "device_export.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include <cstdint>
#include <memory>
#include <mutex>

namespace device {

// Forward declaration
class GPUDevice;
struct AllocatedBuffer;

/**
 * @brief Per-device registry of GPU images exposed as bindless descriptor
 *        arrays.
 *
 * Maintains separate growable arrays for each ImageKind (images2D,
 * atlases, maps).  Each registered image receives a stable
 * ImageHandle whose `index` field is the element in the
 * corresponding descriptor array.
 *
 * Descriptor set layout (set = kBindlessSet, per-device):
 *   binding 0  –  sampler (shared immutable)
 *   binding 1  –  SSBO   TextureRecord[]
 *   binding 2  –  SSBO   TextureLayer[]
 *   binding 3  –  sampled images (images2D), partially bound
 *   binding 4  –  sampled images (atlases), partially bound
 *   binding 5  –  sampled images (maps), variable count (highest binding)
 *
 * Multi-GPU:  each GPUDevice creates its own ImageArrayRegistry.
 * The same logical ImageHandle (returned from the CPU side) can be
 * resolved to different per-device descriptor indices; the handle
 * stores the *logical* index while each device's registry maps it
 * to its own descriptor write.
 *
 * Thread safety:
 *   registerImage() can be called from any thread; it only touches
 *   the CPU-side vectors.  commitDescriptors() performs Vulkan calls
 *   and must be called from the device/render thread.
 */
class DEVICE_API ImageArrayRegistry {
public:
  /// Descriptor set index used by the bindless set (set 1 in pipelines)
  static constexpr uint32_t kBindlessSet = 1;

  /// Bindings inside the bindless descriptor set.
  /// Image arrays are placed at the highest binding numbers so that
  /// VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT (which the
  /// Vulkan spec requires to be on the highest binding) lands on the
  /// last image array (kBindingMaps).
  static constexpr uint32_t kBindingSampler = 0;
  static constexpr uint32_t kBindingTextureRecords = 1;
  static constexpr uint32_t kBindingTextureLayers = 2;
  static constexpr uint32_t kBindingImages2D = 3;
  static constexpr uint32_t kBindingAtlases = 4;
  static constexpr uint32_t kBindingMaps = 5;

  /// Upper bound for the variable-count descriptor arrays.
  /// The descriptor pool is sized for this many images per kind.
  /// Exceeding this count requires recreating the pool.  The actual
  /// descriptor count grows with registrations up to this cap.
  static constexpr uint32_t kMaxImages = 4096;

  ImageArrayRegistry() = default;
  ~ImageArrayRegistry() = default;

  ImageArrayRegistry(const ImageArrayRegistry &) = delete;
  ImageArrayRegistry &operator=(const ImageArrayRegistry &) = delete;

  // ------------------------------------------------------------------
  // Initialisation
  // ------------------------------------------------------------------

  /**
   * @brief Initialise descriptor pool, set layout, and allocate the
   *        bindless descriptor set for this device.
   *
   * @param device  The GPU device that owns these descriptors
   * @return true on success
   */
  bool initialize(GPUDevice &device);

  /**
   * @brief Release all Vulkan resources
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

  // ------------------------------------------------------------------
  // Image registration  (thread-safe, CPU-only)
  // ------------------------------------------------------------------

  /**
   * @brief Register an already-uploaded AllocatedImage and receive
   *        a stable ImageHandle.
   *
   * The image must already have a valid VkImageView.  It is the
   * caller's responsibility to keep the AllocatedImage alive for
   * as long as the handle is in use.
   *
   * Only the original source image should be registered — never
   * mip-generated variants or atlas sub-regions.  All atlas cropping,
   * UV transforms, tiling, rotations, and layer compositing are done
   * at render time in shaders via TextureLayer data.
   *
   * Prefers reusing freed slots from removeImage() before appending.
   *
   * @param kind   Which image array to place the image in
   * @param view   Vulkan image view
   * @return ImageHandle with the descriptor array index
   */
  [[nodiscard]] ImageHandle registerImage(ImageKind kind, vk::ImageView view);

  /**
   * @brief Remove a previously registered image, freeing its slot
   *
   * The slot is marked as a tombstone and can be reused by future
   * registerImage() calls. A partial descriptor update is scheduled
   * to unbind the slot (writes a null image view).
   *
   * The ImageHandle becomes invalid after removal.
   *
   * Multi-GPU note: when removing an image on one device, it is the
   * caller's responsibility to perform the same removal on secondary
   * devices.
   *
   * @param kind   Which image array the handle belongs to
   * @param handle ImageHandle returned by registerImage()
   */
  void removeImage(ImageKind kind, ImageHandle handle);

  // ------------------------------------------------------------------
  // Descriptor commit  (must be called from device thread)
  // ------------------------------------------------------------------

  /**
   * @brief Write all pending image registrations into the Vulkan
   *        descriptor set.
   *
   * Also binds the SSBO buffers for TextureRecord[] and TextureLayer[]
   * if provided.
   *
   * @param device        GPU device
   * @param sampler       The shared sampler to write into binding 3
   * @param recordBuffer  (optional) SSBO for TextureRecord table
   * @param layerBuffer   (optional) SSBO for TextureLayer table
   */
  void commitDescriptors(GPUDevice &device, vk::Sampler sampler,
                         const AllocatedBuffer *recordBuffer = nullptr,
                         const AllocatedBuffer *layerBuffer = nullptr);

  // ------------------------------------------------------------------
  // Accessors
  // ------------------------------------------------------------------

  [[nodiscard]] vk::DescriptorSet getDescriptorSet() const;
  [[nodiscard]] vk::DescriptorSetLayout getDescriptorSetLayout() const;

  /**
   * @brief Query whether descriptor indexing is supported on this device
   *
   * When false, the registry operates in a limited fallback mode
   * (no variable-count descriptors, capped at a small fixed array).
   */
  [[nodiscard]] bool isBindlessSupported() const { return bindlessSupported_; }

  [[nodiscard]] uint32_t getImageCount(ImageKind kind) const;

  /**
   * @brief Create the descriptor set layout that pipelines must
   *        include as set = kBindlessSet.
   *
   * This is a static helper so that Material / pipeline creation
   * code can reference the layout even before a registry instance
   * is fully populated.
   *
   * @param device             GPU device
   * @param bindlessSupported  Whether to use variable-count arrays
   * @return Owning descriptor set layout
   */
  static std::unique_ptr<vk::raii::DescriptorSetLayout>
  createBindlessSetLayout(GPUDevice &device, bool bindlessSupported);

private:
  /// One entry per registered image (pending or committed)
  struct ImageEntry {
    vk::ImageView view;
    bool committed = false;
    bool dirty = false;     ///< Needs descriptor update (added or removed)
    bool tombstone = false; ///< Slot freed by removeImage()
  };

  static constexpr uint32_t kKindCount =
      static_cast<uint32_t>(ImageKind::eCount);

  std::array<std::vector<ImageEntry>, kKindCount> imageArrays_;
  std::array<std::vector<uint32_t>, kKindCount>
      freeLists_; ///< Free slot indices per kind

  std::unique_ptr<vk::raii::DescriptorPool> descriptorPool_;
  std::unique_ptr<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
  std::vector<vk::raii::DescriptorSet> descriptorSets_;

  bool bindlessSupported_ = false;
  bool initialized_ = false;
  mutable std::mutex registryMutex_;
};

} // namespace device

#endif // IMAGE_ARRAY_REGISTRY_H_
