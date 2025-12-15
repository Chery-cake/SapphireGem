#include "texture.h"
#include <print>

render::Texture::Texture(const std::vector<device::LogicalDevice *> &devices,
                         const TextureCreateInfo &createInfo)
    : identifier(createInfo.identifier), type(createInfo.type),
      imagePath(createInfo.imagePath), atlasRegions(createInfo.atlasRegions),
      logicalDevices(devices) {

  // Create the image object
  Image::ImageCreateInfo imageInfo = {.identifier = identifier + "_image",
                                      .format = vk::Format::eR8G8B8A8Srgb,
                                      .usage =
                                          vk::ImageUsageFlagBits::eTransferDst |
                                          vk::ImageUsageFlagBits::eSampled};

  image = std::make_shared<Image>(devices, imageInfo);
}

render::Texture::~Texture() {
  std::print("Texture - {} - destructor executed\n", identifier);
}

void render::Texture::generate_atlas_regions_grid(uint32_t rows,
                                                  uint32_t cols) {
  if (!image || image->get_width() == 0 || image->get_height() == 0) {
    std::print(stderr, "Cannot generate atlas regions: image not loaded\n");
    return;
  }

  atlasRegions.clear();

  uint32_t imageWidth = image->get_width();
  uint32_t imageHeight = image->get_height();

  uint32_t regionWidth = imageWidth / cols;
  uint32_t regionHeight = imageHeight / rows;

  for (uint32_t row = 0; row < rows; ++row) {
    for (uint32_t col = 0; col < cols; ++col) {
      AtlasRegion region;
      region.name = "tile_" + std::to_string(row) + "_" + std::to_string(col);

      float uvMinX = static_cast<float>(col * regionWidth) / imageWidth;
      float uvMinY = static_cast<float>(row * regionHeight) / imageHeight;
      float uvMaxX = static_cast<float>((col + 1) * regionWidth) / imageWidth;
      float uvMaxY = static_cast<float>((row + 1) * regionHeight) / imageHeight;

      region.uvMin = glm::vec2(uvMinX, uvMinY);
      region.uvMax = glm::vec2(uvMaxX, uvMaxY);
      region.width = regionWidth;
      region.height = regionHeight;

      atlasRegions.push_back(region);
    }
  }

  std::print("Texture - {} - generated {}x{} grid atlas ({} regions)\n",
             identifier, rows, cols, atlasRegions.size());
}

bool render::Texture::load() {
  if (imagePath.empty()) {
    std::print(stderr, "Texture - {} - no image path specified\n", identifier);
    return false;
  }

  // Load the image from file
  if (!image->load_from_file(imagePath)) {
    std::print(stderr, "Texture - {} - failed to load image from {}\n",
               identifier, imagePath);
    return false;
  }

  // Upload to GPU
  if (!image->update_gpu_data()) {
    std::print(stderr, "Texture - {} - failed to upload to GPU\n", identifier);
    return false;
  }

  std::print("Texture - {} - loaded successfully from {}\n", identifier,
             imagePath);
  return true;
}

void render::Texture::add_atlas_region(const std::string &name,
                                       const glm::vec2 &uvMin,
                                       const glm::vec2 &uvMax, uint32_t width,
                                       uint32_t height) {
  AtlasRegion region = {.name = name,
                        .uvMin = uvMin,
                        .uvMax = uvMax,
                        .width = width,
                        .height = height};

  atlasRegions.push_back(region);
  std::print("Texture - {} - added atlas region: {}\n", identifier, name);
}

void render::Texture::generate_grid_atlas(uint32_t rows, uint32_t cols) {
  generate_atlas_regions_grid(rows, cols);
}

const render::Texture::AtlasRegion *
render::Texture::get_atlas_region(const std::string &name) const {
  for (const auto &region : atlasRegions) {
    if (region.name == name) {
      return &region;
    }
  }
  return nullptr;
}

const std::vector<render::Texture::AtlasRegion> &
render::Texture::get_atlas_regions() const {
  return atlasRegions;
}

void render::Texture::set_color_tint(const glm::vec4 &tint) {
  if (image) {
    image->set_color_tint(tint);
    std::print("Texture - {} - applied color tint\n", identifier);
  }
}

void render::Texture::rotate_90_clockwise() {
  if (image) {
    image->rotate_90_clockwise();
    std::print("Texture - {} - rotated 90 degrees clockwise\n", identifier);
  }
}

void render::Texture::rotate_90_counter_clockwise() {
  if (image) {
    image->rotate_90_counter_clockwise();
    std::print("Texture - {} - rotated 90 degrees counter-clockwise\n",
               identifier);
  }
}

void render::Texture::rotate_180() {
  if (image) {
    image->rotate_180();
    std::print("Texture - {} - rotated 180 degrees\n", identifier);
  }
}

bool render::Texture::update_gpu() {
  if (image) {
    bool success = image->update_gpu_data();
    if (success) {
      std::print("Texture - {} - updated GPU data\n", identifier);
    }
    return success;
  }
  return false;
}

const std::string &render::Texture::get_identifier() const {
  return identifier;
}

render::Texture::TextureType render::Texture::get_type() const { return type; }

std::shared_ptr<render::Image> render::Texture::get_image() const {
  return image;
}

uint32_t render::Texture::get_width() const {
  return image ? image->get_width() : 0;
}

uint32_t render::Texture::get_height() const {
  return image ? image->get_height() : 0;
}
