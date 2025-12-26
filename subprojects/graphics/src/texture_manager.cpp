#include "texture_manager.h"
#include <cstdio>
#include <print>

render::TextureManager::TextureManager(
    const device::DeviceManager *deviceManager)
    : deviceManager(deviceManager) {
  std::print("TextureManager - initialized\n");
}

render::TextureManager::~TextureManager() {
  std::lock_guard lock(managerMutex);
  textures.clear();
  layeredTextures.clear();
  std::print("TextureManager - destroyed\n");
}

render::Texture *
render::TextureManager::create_texture(const std::string &identifier,
                                       const std::string &filepath) {
  std::lock_guard lock(managerMutex);

  if (textures.find(identifier) != textures.end()) {
    std::print("Texture with identifier '{}' already exists\n", identifier);
    return textures[identifier].get();
  }

  Texture::TextureCreateInfo createInfo = {.identifier = identifier,
                                           .type = Texture::TextureType::SINGLE,
                                           .imagePath = filepath};

  auto devices = deviceManager->get_all_logical_devices();
  auto texture = std::make_unique<Texture>(devices, createInfo);

  // Load the image
  if (!texture->get_image()->load_from_file(filepath)) {
    std::print(stderr, "Failed to load texture from file: {}\n", filepath);
    return nullptr;
  }

  // Upload to GPU
  if (!texture->update_gpu()) {
    std::print(stderr, "Failed to upload texture to GPU\n");
    return nullptr;
  }

  Texture *ptr = texture.get();
  textures[identifier] = std::move(texture);

  std::print("TextureManager - created texture: {}\n", identifier);
  return ptr;
}

render::Texture *
render::TextureManager::create_texture_atlas(const std::string &identifier,
                                             const std::string &filepath,
                                             uint32_t rows, uint32_t cols) {
  std::lock_guard lock(managerMutex);

  if (textures.find(identifier) != textures.end()) {
    std::print("Texture with identifier '{}' already exists\n", identifier);
    return textures[identifier].get();
  }

  Texture::TextureCreateInfo createInfo = {.identifier = identifier,
                                           .type = Texture::TextureType::ATLAS,
                                           .imagePath = filepath};

  auto devices = deviceManager->get_all_logical_devices();
  auto texture = std::make_unique<Texture>(devices, createInfo);

  // Load the image
  if (!texture->get_image()->load_from_file(filepath)) {
    std::print(stderr, "Failed to load texture atlas from file: {}\n",
               filepath);
    return nullptr;
  }

  // Generate atlas regions
  texture->generate_grid_atlas(rows, cols);

  // Upload to GPU
  if (!texture->update_gpu()) {
    std::print(stderr, "Failed to upload texture atlas to GPU\n");
    return nullptr;
  }

  Texture *ptr = texture.get();
  textures[identifier] = std::move(texture);

  std::print("TextureManager - created texture atlas: {} ({}x{})\n", identifier,
             rows, cols);
  return ptr;
}

render::Texture *render::TextureManager::create_texture(
    const Texture::TextureCreateInfo &createInfo) {
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
      std::print(stderr, "Failed to load texture from path in createInfo\n");
      return nullptr;
    }
  }

  Texture *ptr = texture.get();
  textures[createInfo.identifier] = std::move(texture);

  std::print("TextureManager - created texture: {}\n", createInfo.identifier);
  return ptr;
}

void render::TextureManager::remove_texture(const std::string &identifier) {
  std::lock_guard lock(managerMutex);

  auto it = textures.find(identifier);
  if (it != textures.end()) {
    textures.erase(it);
    std::print("TextureManager - removed texture: {}\n", identifier);
  }
}

render::Texture *
render::TextureManager::get_texture(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);

  auto it = textures.find(identifier);
  if (it != textures.end()) {
    return it->second.get();
  }
  return nullptr;
}

bool render::TextureManager::has_texture(const std::string &identifier) const {
  std::lock_guard lock(managerMutex);
  return textures.find(identifier) != textures.end();
}

std::vector<render::Texture *>
render::TextureManager::get_all_textures() const {
  std::lock_guard lock(managerMutex);

  std::vector<Texture *> result;
  result.reserve(textures.size());

  for (const auto &[id, texture] : textures) {
    result.push_back(texture.get());
  }

  return result;
}

render::LayeredTexture *render::TextureManager::create_layered_texture(
    const LayeredTexture::LayeredTextureCreateInfo &createInfo) {
  std::lock_guard lock(managerMutex);

  if (layeredTextures.find(createInfo.identifier) != layeredTextures.end()) {
    std::print("LayeredTexture with identifier '{}' already exists\n",
               createInfo.identifier);
    return layeredTextures[createInfo.identifier].get();
  }

  auto devices = deviceManager->get_all_logical_devices();
  auto layeredTexture = std::make_unique<LayeredTexture>(devices, createInfo);

  // Load all layers
  if (!layeredTexture->load()) {
    std::print(stderr, "Failed to load layered texture: {}\n",
               createInfo.identifier);
    return nullptr;
  }

  LayeredTexture *ptr = layeredTexture.get();
  layeredTextures[createInfo.identifier] = std::move(layeredTexture);

  std::print("TextureManager - created layered texture: {} with {} layers\n",
             createInfo.identifier, createInfo.layers.size());
  return ptr;
}

void render::TextureManager::remove_layered_texture(
    const std::string &identifier) {
  std::lock_guard lock(managerMutex);

  auto it = layeredTextures.find(identifier);
  if (it != layeredTextures.end()) {
    layeredTextures.erase(it);
    std::print("TextureManager - removed layered texture: {}\n", identifier);
  }
}

render::LayeredTexture *render::TextureManager::get_layered_texture(
    const std::string &identifier) const {
  std::lock_guard lock(managerMutex);

  auto it = layeredTextures.find(identifier);
  if (it != layeredTextures.end()) {
    return it->second.get();
  }
  return nullptr;
}

bool render::TextureManager::has_layered_texture(
    const std::string &identifier) const {
  std::lock_guard lock(managerMutex);
  return layeredTextures.find(identifier) != layeredTextures.end();
}

std::vector<render::LayeredTexture *>
render::TextureManager::get_all_layered_textures() const {
  std::lock_guard lock(managerMutex);

  std::vector<LayeredTexture *> result;
  result.reserve(layeredTextures.size());

  for (const auto &[id, texture] : layeredTextures) {
    result.push_back(texture.get());
  }

  return result;
}
