#pragma once

#include "image.h"
#include "logical_device.h"
#include <glm/ext/vector_float2.hpp>
#include <string>
#include <vector>

namespace render {

class Texture {
public:
  enum class TextureType { SINGLE, ATLAS };

  struct AtlasRegion {
    std::string name;
    glm::vec2 uvMin; // Top-left UV coordinates
    glm::vec2 uvMax; // Bottom-right UV coordinates
    uint32_t width;
    uint32_t height;
  };

  struct TextureCreateInfo {
    std::string identifier;
    TextureType type = TextureType::SINGLE;
    std::string imagePath;                 // Path to image file
    std::vector<AtlasRegion> atlasRegions; // For texture atlases
  };

private:
  std::string identifier;
  TextureType type;
  std::string imagePath;
  std::shared_ptr<Image> image;
  std::vector<AtlasRegion> atlasRegions;

  std::vector<device::LogicalDevice *> logicalDevices;

  void generate_atlas_regions_grid(uint32_t rows, uint32_t cols);

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

  // Image manipulation
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
  std::shared_ptr<Image> get_image() const;
  uint32_t get_width() const;
  uint32_t get_height() const;
};

} // namespace render
