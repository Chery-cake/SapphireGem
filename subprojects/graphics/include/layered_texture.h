#pragma once

#include "image.h"
#include "logical_device.h"
#include "tasks.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float4.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace render {

class LayeredTexture {
public:
  struct ImageLayer {
    std::string imagePath;        // Path to the source image file
    std::shared_ptr<Image> image; // The actual image resource (may be shared)
    glm::vec4 tint = glm::vec4(1.0f); // Color tint (default: no tint)
    float rotation = 0.0f;            // Rotation in degrees (0, 90, 180, 270)
    bool visible = true;              // Layer visibility

    ImageLayer() = default;
    ImageLayer(const std::string &path) : imagePath(path) {}
  };

  struct LayeredTextureCreateInfo {
    std::string identifier;
    std::vector<ImageLayer> layers;
  };

private:
  mutable std::mutex textureMutex;

  std::string identifier;
  std::vector<ImageLayer> layers;
  std::vector<device::LogicalDevice *> logicalDevices;

  // Cache of loaded images to avoid loading the same file multiple times
  std::unordered_map<std::string, std::shared_ptr<Image>> imageCache;

  // Composited final image that combines all layers
  std::shared_ptr<Image> compositedImage;

  // Helper to load or retrieve cached image
  std::shared_ptr<Image> load_or_get_cached_image(const std::string &imagePath);

  // Composite all layers into final image
  bool composite_layers();

  // Apply rotation to pixel data
  std::vector<unsigned char>
  apply_rotation(const std::vector<unsigned char> &pixels, uint32_t width,
                 uint32_t height, uint32_t channels, float rotation);

  // Apply tint to pixel data
  std::vector<unsigned char>
  apply_tint(const std::vector<unsigned char> &pixels, uint32_t channels,
             const glm::vec4 &tint);

  // Blend two pixel buffers with alpha blending
  void blend_layer(std::vector<unsigned char> &dst,
                   const std::vector<unsigned char> &src, uint32_t width,
                   uint32_t height);

public:
  LayeredTexture(const std::vector<device::LogicalDevice *> &devices,
                 const LayeredTextureCreateInfo &createInfo);
  ~LayeredTexture();

  // Load all layer images and composite them
  bool load();

  // Add a new layer
  void add_layer(const ImageLayer &layer);

  // Modify a layer
  void set_layer_tint(size_t layerIndex, const glm::vec4 &tint);
  void set_layer_rotation(size_t layerIndex, float rotation);
  void set_layer_visibility(size_t layerIndex, bool visible);

  // Update GPU with current composited image
  bool update_gpu();

  // Recomposite and update
  bool recomposite_and_update();

  // Getters
  const std::string &get_identifier() const;
  std::shared_ptr<Image> get_composited_image() const;
  size_t get_layer_count() const;
  const ImageLayer &get_layer(size_t index) const;
  uint32_t get_width() const;
  uint32_t get_height() const;
};

} // namespace render
