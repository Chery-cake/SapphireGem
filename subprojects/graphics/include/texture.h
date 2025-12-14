#pragma once

#include "image.h"
#include "logical_device.h"
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

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
    std::string imagePath;       // Path to image file
    std::vector<AtlasRegion> atlasRegions; // For texture atlases
  };

private:
  std::string identifier;
  TextureType type;
  std::shared_ptr<Image> image;
  std::vector<AtlasRegion> atlasRegions;

  std::vector<LogicalDevice *> logicalDevices;

  void generate_atlas_regions_grid(uint32_t rows, uint32_t cols);

public:
  Texture(const std::vector<LogicalDevice *> &devices,
          const TextureCreateInfo &createInfo);
  ~Texture();

  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  Texture(Texture &&) = delete;
  Texture &operator=(Texture &&) = delete;

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

  // Getters
  const std::string &get_identifier() const;
  TextureType get_type() const;
  std::shared_ptr<Image> get_image() const;
  uint32_t get_width() const;
  uint32_t get_height() const;
};
