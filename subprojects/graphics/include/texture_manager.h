#pragma once

#include "device_manager.h"
#include "texture.h"
#include <mutex>

class TextureManager {
private:
  mutable std::mutex managerMutex;

  const DeviceManager *deviceManager;

  std::unordered_map<std::string, std::unique_ptr<Texture>> textures;

public:
  TextureManager(const DeviceManager *deviceManager);
  ~TextureManager();

  // Create a texture from file
  Texture *create_texture(const std::string &identifier,
                          const std::string &filepath);

  // Create a texture atlas from file
  Texture *create_texture_atlas(const std::string &identifier,
                                const std::string &filepath, uint32_t rows,
                                uint32_t cols);

  // Create a texture with custom configuration
  Texture *create_texture(const Texture::TextureCreateInfo &createInfo);

  // Remove a texture
  void remove_texture(const std::string &identifier);

  // Get texture by identifier
  Texture *get_texture(const std::string &identifier) const;
  bool has_texture(const std::string &identifier) const;

  // Get all textures
  std::vector<Texture *> get_all_textures() const;
};
