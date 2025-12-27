#pragma once

#include "image.h"
#include "logical_device.h"
#include <glm/ext/vector_float2.hpp>
#include <string>
#include <vector>

namespace render {

class Texture {
public:
  enum class TextureType { SINGLE, ATLAS, LAYERED };

  struct AtlasRegion {
    std::string name;
    glm::vec2 uvMin; // Top-left UV coordinates
    glm::vec2 uvMax; // Bottom-right UV coordinates
    uint32_t width;
    uint32_t height;
  };

  // Layer structure for layered textures
  struct Layer {
    std::string imagePath;            // Path to the source image file
    Image *image = nullptr;           // The actual image resource (raw pointer,
                                      // managed by cache)
    glm::vec4 tint = glm::vec4(1.0f); // Color tint (default: no tint)
    float rotation = 0.0f;            // Rotation in degrees (0, 90, 180, 270)
    bool visible = true;              // Layer visibility
    Texture *nextLayer = nullptr;     // Reference to the layer above this one

    Layer() = default;
    Layer(const std::string &path) : imagePath(path) {}
  };

  struct TextureCreateInfo {
    std::string identifier;
    TextureType type = TextureType::SINGLE;
    std::string imagePath; // Path to image file (for SINGLE type)
    std::vector<AtlasRegion> atlasRegions; // For texture atlases
    std::vector<Layer> layers;             // For layered textures
  };

private:
  mutable std::mutex textureMutex;

  std::string identifier;
  TextureType type;
  std::string imagePath;
  std::unique_ptr<Image> image;
  std::vector<AtlasRegion> atlasRegions;

  // Atlas grid configuration (for reloading)
  uint32_t atlasRows = 0;
  uint32_t atlasCols = 0;

  // Layered texture support
  std::vector<Layer> layers;
  std::unordered_map<std::string, std::unique_ptr<Image>> imageCache;
  std::unique_ptr<Image> compositedImage;

  std::vector<device::LogicalDevice *> logicalDevices;

  // Helper methods
  void generate_atlas_regions_grid(uint32_t rows, uint32_t cols);

  // Layered texture helpers
  Image *load_or_get_cached_image(const std::string &imagePath);
  bool composite_layers();
  std::vector<unsigned char>
  apply_rotation(const std::vector<unsigned char> &pixels, uint32_t width,
                 uint32_t height, uint32_t channels, float rotation);
  std::vector<unsigned char>
  apply_tint(const std::vector<unsigned char> &pixels, uint32_t channels,
             const glm::vec4 &tint);
  void blend_layer(std::vector<unsigned char> &dst,
                   const std::vector<unsigned char> &src, uint32_t width,
                   uint32_t height);

public:
  Texture(const std::vector<device::LogicalDevice *> &devices,
          const TextureCreateInfo &createInfo);
  ~Texture();

  // Load texture
  bool load();

  // For texture atlases
  void add_atlas_region(const std::string &name, const glm::vec2 &uvMin,
                        const glm::vec2 &uvMax, uint32_t width,
                        uint32_t height);
  void generate_grid_atlas(uint32_t rows, uint32_t cols);

  const AtlasRegion *get_atlas_region(const std::string &name) const;
  const std::vector<AtlasRegion> &get_atlas_regions() const;

  // Layer management (for LAYERED textures)
  void add_layer(const Layer &layer);
  void set_layer_tint(size_t layerIndex, const glm::vec4 &tint);
  void set_layer_rotation(size_t layerIndex, float rotation);
  void set_layer_visibility(size_t layerIndex, bool visible);
  size_t get_layer_count() const;
  const Layer &get_layer(size_t index) const;
  bool recomposite_and_update();

  // Image manipulation (for SINGLE textures)
  void set_color_tint(const glm::vec4 &tint);
  void rotate_90_clockwise();
  void rotate_90_counter_clockwise();
  void rotate_180();

  // Apply changes to GPU
  bool update_gpu();

  // Reload texture from original source file (resets all modifications)
  bool reload();

  // Getters
  const std::string &get_identifier() const;
  TextureType get_type() const;
  Image *get_image() const;
  uint32_t get_width() const;
  uint32_t get_height() const;
};

} // namespace render
