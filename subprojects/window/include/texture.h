#ifndef TEXTURE_H_
#define TEXTURE_H_

#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace window {

/**
 * @brief Tag for identifying images within a texture
 *
 * Stores metadata about a single image source. Must have static storage
 * duration (constexpr, static, or global) when used with ResourceRegistry.
 */
struct WINDOW_API ImageTag {
  const char *name;
  const char *path;    // File path or atlas source path
  uint32_t width = 0;  // 0 = auto-detect from file
  uint32_t height = 0; // 0 = auto-detect from file
  uint32_t atlasX = 0; // X offset in atlas (0 if not atlas)
  uint32_t atlasY = 0; // Y offset in atlas (0 if not atlas)
  uint32_t atlasW = 0; // Width in atlas (0 = full image)
  uint32_t atlasH = 0; // Height in atlas (0 = full image)
  bool isAtlasRegion = false;

  constexpr ImageTag(const char *n, const char *p, uint32_t w = 0,
                     uint32_t h = 0)
      : name(n), path(p), width(w), height(h) {}

  constexpr ImageTag(const char *n, const char *p, uint32_t ax, uint32_t ay,
                     uint32_t aw, uint32_t ah)
      : name(n), path(p), atlasX(ax), atlasY(ay), atlasW(aw), atlasH(ah),
        isAtlasRegion(true) {}
};

/**
 * @brief Per-layer image transform modifiers
 *
 * Each layer in a texture can have individual transforms applied.
 */
struct WINDOW_API ImageTransform {
  float rotation = 0.0f;                                // Rotation in radians
  float scaleX = 1.0f;                                  // Scale X
  float scaleY = 1.0f;                                  // Scale Y
  float offsetX = 0.0f;                                 // UV offset X
  float offsetY = 0.0f;                                 // UV offset Y
  std::array<float, 4> tint = {1.0f, 1.0f, 1.0f, 1.0f}; // RGBA tint modifier
  float opacity = 1.0f;                                 // Layer opacity
};

/**
 * @brief Describes a single layer within a TextureTag
 *
 * Binds an ImageTag with its default transform at tag definition time.
 */
struct DEVICE_API TextureLayerInfo {
  const ImageTag *imageTag = nullptr;
  ImageTransform defaultTransform;

  constexpr TextureLayerInfo() = default;
  constexpr TextureLayerInfo(const ImageTag *img) : imageTag(img) {}
  constexpr TextureLayerInfo(const ImageTag *img,
                             const ImageTransform &transform)
      : imageTag(img), defaultTransform(transform) {}
};

/**
 * @brief Tag for identifying textures in the resource system
 *
 * A texture tag describes a single or multi-layer texture composition.
 * It can embed pointers to ImageTags and default transforms for each layer,
 * providing a complete description of the texture's contents at tag
 * definition time.
 *
 * Example:
 * @code
 *   static constexpr ImageTag GRASS_IMAGE{"grass", "textures/grass.png"};
 *   static constexpr ImageTag MOSS_IMAGE{"moss", "textures/moss.png"};
 *
 *   // Layers defined at tag level
 *   static const TextureLayerInfo TERRAIN_LAYERS[] = {
 *     {&GRASS_IMAGE},
 *     {&MOSS_IMAGE},
 *   };
 *
 *   static const TextureTag TERRAIN_TEXTURE{
 *     "terrain", TERRAIN_LAYERS, 2
 *   };
 * @endcode
 */
struct WINDOW_API TextureTag {
  const char *name;
  const TextureLayerInfo *layers = nullptr; // Pointer to array of layer infos
  uint32_t layerCount = 0;

  constexpr TextureTag(const char *n, const TextureLayerInfo *layerInfos,
                       uint32_t count)
      : name(n), layers(layerInfos), layerCount(count) {}
};

/**
 * @brief A single image layer within a texture
 */
struct WINDOW_API TextureLayer {
  const ImageTag *imageTag = nullptr;
  ImageTransform transform;
  device::AllocatedImage gpuImage;
  bool loaded = false;
};

/**
 * @brief Manages a single or multi-layer texture
 *
 * A texture can contain one or more image layers, where each layer can be
 * individually transformed (rotated, scaled, tinted). Images can come from
 * individual files or from regions of a texture atlas.
 *
 * Textures can be shared across materials to save GPU memory.
 *
 * Thread-safe: all mutable operations are protected by mutex.
 */
class WINDOW_API Texture {
public:
  explicit Texture(const TextureTag &tag);
  ~Texture();

  // Disable copy, enable move
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  Texture(Texture &&) noexcept;
  Texture &operator=(Texture &&) noexcept;

  /**
   * @brief Add a layer to this texture
   * @param imageTag Tag identifying the image source
   * @param transform Initial transform for this layer
   * @return Layer index
   */
  uint32_t addLayer(const ImageTag *imageTag,
                    const ImageTransform &transform = {});

  /**
   * @brief Set the transform for a specific layer
   * @param layerIndex Index of the layer
   * @param transform New transform to apply
   * @return true if layer exists and transform was set
   */
  bool setLayerTransform(uint32_t layerIndex, const ImageTransform &transform);

  /**
   * @brief Get the transform for a specific layer
   * @param layerIndex Index of the layer
   * @return Pointer to transform, or nullptr if layer doesn't exist
   */
  const ImageTransform *getLayerTransform(uint32_t layerIndex) const;

  /**
   * @brief Upload all layers to the GPU
   * @param allocator VMA allocator for image creation
   * @param device GPU device (reserved for future command buffer operations
   *               such as image layout transitions and staging uploads)
   * @return true if all layers were uploaded successfully
   */
  bool upload(device::VMAAllocator &allocator, device::GPUDevice &device);

  /**
   * @brief Release GPU resources
   */
  void release();

  /**
   * @brief Create a Vulkan sampler for this texture
   * @param device GPU device
   * @return true if sampler was created
   */
  bool createSampler(device::GPUDevice &device);

  // Getters
  [[nodiscard]] const std::string &getName() const { return name_; }
  [[nodiscard]] uint32_t getLayerCount() const;
  [[nodiscard]] bool isUploaded() const { return uploaded_; }
  [[nodiscard]] vk::Sampler getSampler() const;
  [[nodiscard]] const TextureLayer *getLayer(uint32_t index) const;

private:
  std::string name_;
  std::vector<TextureLayer> layers_;
  std::unique_ptr<vk::raii::Sampler> sampler_;
  bool uploaded_ = false;
  mutable std::mutex textureMutex_;
};

} // namespace window

#endif // TEXTURE_H_
