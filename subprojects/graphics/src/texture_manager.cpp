#include "texture_manager.h"
#include <print>

TextureManager::TextureManager(const DeviceManager *deviceManager)
    : deviceManager(deviceManager) {
  std::print("TextureManager - initialized\n");
}

TextureManager::~TextureManager() {
  std::lock_guard lock(managerMutex);
  textures.clear();
  std::print("TextureManager - destroyed\n");
}

Texture *TextureManager::create_texture(const std::string &identifier,
                                        const std::string &filepath) {
  std::lock_guard lock(managerMutex);

  if (textures.find(identifier) != textures.end()) {
    std::print("Texture with identifier '{}' already exists\n", identifier);
    return textures[identifier].get();
  }

  Texture::TextureCreateInfo createInfo;
  createInfo.identifier = identifier;
  createInfo.type = Texture::TextureType::SINGLE;
  createInfo.imagePath = filepath;

  auto devices = deviceManager->get_all_logical_devices();
  auto texture = std::make_unique<Texture>(devices, createInfo);

  // Load the image
  if (!texture->get_image()->load_from_file(filepath)) {
    std::print("Failed to load texture from file: {}\n", filepath);
    return nullptr;
  }

  // Upload to GPU
  if (!texture->update_gpu()) {
    std::print("Failed to upload texture to GPU\n");
    return nullptr;
  }

  Texture *ptr = texture.get();
  textures[identifier] = std::move(texture);

  std::print("TextureManager - created texture: {}\n", identifier);
  return ptr;
}

Texture *TextureManager::create_texture_atlas(const std::string &identifier,
                                              const std::string &filepath,
                                              uint32_t rows, uint32_t cols) {
  std::lock_guard lock(managerMutex);

  if (textures.find(identifier) != textures.end()) {
    std::print("Texture with identifier '{}' already exists\n", identifier);
    return textures[identifier].get();
  }

  Texture::TextureCreateInfo createInfo;
  createInfo.identifier = identifier;
  createInfo.type = Texture::TextureType::ATLAS;
  createInfo.imagePath = filepath;

  auto devices = deviceManager->get_all_logical_devices();
  auto texture = std::make_unique<Texture>(devices, createInfo);

  // Load the image
  if (!texture->get_image()->load_from_file(filepath)) {
    std::print("Failed to load texture atlas from file: {}\n", filepath);
    return nullptr;
  }

  // Generate atlas regions
  texture->generate_grid_atlas(rows, cols);

  // Upload to GPU
  if (!texture->update_gpu()) {
    std::print("Failed to upload texture atlas to GPU\n");
    return nullptr;
  }

  Texture *ptr = texture.get();
  textures[identifier] = std::move(texture);

  std::print("TextureManager - created texture atlas: {} ({}x{})\n",
             identifier, rows, cols);
  return ptr;
}

Texture *
TextureManager::create_texture(const Texture::TextureCreateInfo &createInfo) {
  std::lock_guard lock(managerMutex);

  if (textures.find(createInfo.identifier) != textures.end()) {
    std::print("Texture with identifier '{}' already exists\n",
               createInfo.identifier);
    return textures[createInfo.identifier].get();
  }

  auto devices = deviceManager->get_all_logical_devices();
  auto texture = std::make_unique<Texture>(devices, createInfo);

  // Load from file if imagePath is provided
  if (!createInfo.imagePath.empty()) {
    if (!texture->load()) {
      std::print("Failed to load texture from path in createInfo\n");
      return nullptr;
    }
  }

  Texture *ptr = texture.get();
  textures[createInfo.identifier] = std::move(texture);

  std::print("TextureManager - created texture: {}\n", createInfo.identifier);
  return ptr;
}

void TextureManager::remove_texture(const std::string &identifier) {
  std::lock_guard lock(managerMutex);

  auto it = textures.find(identifier);
  if (it != textures.end()) {
    textures.erase(it);
    std::print("TextureManager - removed texture: {}\n", identifier);
  }
}

Texture *TextureManager::get_texture(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);

  auto it = textures.find(identifier);
  if (it != textures.end()) {
    return it->second.get();
  }
  return nullptr;
}

bool TextureManager::has_texture(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);
  return textures.find(identifier) != textures.end();
}

std::vector<Texture *> TextureManager::get_all_textures() const {
  std::lock_guard lock(managerMutex);

  std::vector<Texture *> result;
  result.reserve(textures.size());

  for (const auto &[id, texture] : textures) {
    result.push_back(texture.get());
  }

  return result;
}
