#ifndef TEXTURE_H_
#define TEXTURE_H_

#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "window_export.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <variant>

namespace window {

/**
 * @brief Tag describing a texture atlas (single GPU buffer per atlas)
 *
 * An atlas is a single image file that contains multiple sub-images.
 * The atlas buffer is loaded once and shared across all regions.
 * Must have static storage duration when used with constexpr tags.
 */
struct WINDOW_API AtlasTag {
  const char *name;
  const char *path;
  uint32_t width;
  uint32_t height;

  constexpr AtlasTag(const char *n, const char *p, uint32_t w, uint32_t h)
      : name(n), path(p), width(w), height(h) {}
};

/**
 * @brief Source descriptor: image loaded from a standalone file
 */
struct WINDOW_API ImageFromFile {
  const char *name;
  const char *path;
  uint32_t width = 0;  // 0 = auto-detect from file
  uint32_t height = 0; // 0 = auto-detect from file
  constexpr ImageFromFile(const char *n, const char *p, uint32_t w = 0,
                          uint32_t h = 0)
      : name(n), path(p), width(w), height(h) {}
};

/**
 * @brief Source descriptor: image region within an atlas
 *
 * References an AtlasTag and specifies the sub-region coordinates.
 * The atlas buffer is reused—never duplicated.
 */
struct WINDOW_API ImageFromAtlasRegion {
  const char *name;
  const AtlasTag *atlas; // Must outlive this descriptor
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;

  constexpr ImageFromAtlasRegion(const char *n, const AtlasTag *a, uint32_t ax,
                                 uint32_t ay, uint32_t aw, uint32_t ah)
      : name(n), atlas(a), x(ax), y(ay), width(aw), height(ah) {}
};

/**
 * @brief Tag for identifying images within a texture
 *
 * Uses std::variant to distinguish between standalone file images
 * and atlas region images. Must have static storage duration when
 * used with ResourceRegistry.
 */
struct WINDOW_API ImageTag {
  std::variant<ImageFromFile, ImageFromAtlasRegion> source;

  ImageTag(const ImageFromFile &file) : source(file) {}
  ImageTag(const ImageFromAtlasRegion &region) : source(region) {}

  [[nodiscard]] const char *getName() const {
    return std::visit([](const auto &s) { return s.name; }, source);
  }

  [[nodiscard]] bool isAtlasRegion() const {
    return std::holds_alternative<ImageFromAtlasRegion>(source);
  }
};

/**
 * @brief Per-layer image transform and modifier parameters
 *
 * Each layer in a texture can have individual modifiers applied.
 * These can be set CPU-side and passed to shaders for runtime effects.
 */
struct WINDOW_API ImageTransform {
  float rotation = 0.0f;                                // Rotation in radians
  float scaleX = 1.0f;                                  // Scale X
  float scaleY = 1.0f;                                  // Scale Y
  float offsetX = 0.0f;                                 // UV offset X
  float offsetY = 0.0f;                                 // UV offset Y
  std::array<float, 4> tint = {1.0f, 1.0f, 1.0f, 1.0f}; // RGBA tint modifier
  float opacity = 1.0f;                                 // Layer opacity
  float animSpeed = 0.0f; // Animation speed (0 = static)
  float animPhase = 0.0f; // Animation phase offset
};

/**
 * @brief Describes a single layer within a TextureTag
 *
 * Binds an ImageTag with its default transform at tag definition time.
 *
 * Note: Constructors are not constexpr because ImageTag contains
 * std::variant which may not be constexpr-constructible in all contexts.
 */
struct WINDOW_API TextureLayerInfo {
  const ImageTag *imageTag = nullptr;
  ImageTransform defaultTransform;

  TextureLayerInfo() = default;
  TextureLayerInfo(const ImageTag *img) : imageTag(img) {}
  TextureLayerInfo(const ImageTag *img, const ImageTransform &transform)
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
 *   AtlasTag atlas{"spritesheet", "sprites/sheet.png", 1024, 1024};
 *   ImageTag heroTag{ImageFromFile{"hero", "hero.png", 64, 64}};
 *   ImageTag enemyTag{ImageFromAtlasRegion{"enemy", &atlas, 128, 0, 32, 32}};
 *
 *   static const TextureLayerInfo LAYERS[] = { {&heroTag}, {&enemyTag} };
 *   static const TextureTag TEX{"my_tex", LAYERS, 2};
 *
 *   Texture tex(TEX);
 *   tex.setLayerTransform(1,  ... modifiers ... );
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
 * Atlas images are loaded once and shared across all regions referencing them,
 * avoiding memory duplication.
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
   *
   * Creates GPU images and image views for each layer, then transitions
   * each image from VK_IMAGE_LAYOUT_UNDEFINED to
   * VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL using a one-time command
   * buffer so the images are ready for shader sampling.
   *
   * Atlas images are loaded once and reused for all regions referencing them.
   *
   * @param allocator VMA allocator for image creation
   * @param device GPU device for command buffer submission
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

  /**
   * @brief Create a 1×1 white fallback texture for missing descriptor
   * bindings
   *
   * Used to provide a valid combined image sampler when a texture slot
   * is unbound, preventing Vulkan validation errors from null descriptors.
   *
   * @param allocator VMA allocator for image creation
   * @param device GPU device for command buffer submission
   * @return Shared pointer to fallback texture, or nullptr on failure
   */
  static std::shared_ptr<Texture>
  createFallback(device::VMAAllocator &allocator, device::GPUDevice &device);

private:
  std::string name_;
  std::vector<TextureLayer> layers_;
  std::unique_ptr<vk::raii::Sampler> sampler_;
  bool uploaded_ = false;
  mutable std::mutex textureMutex_;
};

} // namespace window

#endif // TEXTURE_H_
