#include "layered_texture.h"
#include "tasks.h"
#include <print>
#include <algorithm>
#include <cstring>
#include <future>

render::LayeredTexture::LayeredTexture(
    const std::vector<device::LogicalDevice *> &devices,
    const LayeredTextureCreateInfo &createInfo)
    : identifier(createInfo.identifier), layers(createInfo.layers),
      logicalDevices(devices) {
  
  std::print("LayeredTexture - {} - created with {} layers\n", 
             identifier, layers.size());
}

render::LayeredTexture::~LayeredTexture() {
  std::print("LayeredTexture - {} - destructor executed\n", identifier);
}

std::shared_ptr<render::Image> 
render::LayeredTexture::load_or_get_cached_image(const std::string &imagePath) {
  // Check cache first (with lock)
  {
    std::lock_guard lock(textureMutex);
    auto it = imageCache.find(imagePath);
    if (it != imageCache.end()) {
      std::print("LayeredTexture - {} - using cached image for {}\n", 
                 identifier, imagePath);
      return it->second;
    }
  }
  
  // Create new image
  Image::ImageCreateInfo imageInfo = {
    .identifier = identifier + "_" + imagePath,
    .format = vk::Format::eR8G8B8A8Srgb,
    .usage = vk::ImageUsageFlagBits::eTransferDst | 
             vk::ImageUsageFlagBits::eSampled
  };
  
  auto image = std::make_shared<Image>(logicalDevices, imageInfo);
  
  if (!image->load_from_file(imagePath)) {
    std::print(stderr, "LayeredTexture - {} - failed to load image from {}\n",
               identifier, imagePath);
    return nullptr;
  }
  
  // Cache it (with lock)
  {
    std::lock_guard lock(textureMutex);
    imageCache[imagePath] = image;
  }
  std::print("LayeredTexture - {} - loaded and cached image from {}\n",
             identifier, imagePath);
  
  return image;
}

std::vector<unsigned char> 
render::LayeredTexture::apply_rotation(const std::vector<unsigned char> &pixels,
                                      uint32_t width, uint32_t height,
                                      uint32_t channels, float rotation) {
  // Normalize rotation to 0, 90, 180, 270
  int rot = static_cast<int>(rotation) % 360;
  if (rot < 0) rot += 360;
  
  // Round to nearest 90 degrees
  rot = ((rot + 45) / 90) * 90;
  
  if (rot == 0) {
    return pixels; // No rotation needed
  }
  
  std::vector<unsigned char> rotated;
  
  if (rot == 90) {
    // Rotate 90 degrees clockwise
    rotated.resize(pixels.size());
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        uint32_t srcIndex = (y * width + x) * channels;
        uint32_t dstX = height - 1 - y;
        uint32_t dstY = x;
        uint32_t dstIndex = (dstY * height + dstX) * channels;
        
        for (uint32_t c = 0; c < channels; ++c) {
          rotated[dstIndex + c] = pixels[srcIndex + c];
        }
      }
    }
  } else if (rot == 180) {
    // Rotate 180 degrees
    rotated.resize(pixels.size());
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        uint32_t srcIndex = (y * width + x) * channels;
        uint32_t dstX = width - 1 - x;
        uint32_t dstY = height - 1 - y;
        uint32_t dstIndex = (dstY * width + dstX) * channels;
        
        for (uint32_t c = 0; c < channels; ++c) {
          rotated[dstIndex + c] = pixels[srcIndex + c];
        }
      }
    }
  } else if (rot == 270) {
    // Rotate 270 degrees clockwise (90 counter-clockwise)
    rotated.resize(pixels.size());
    for (uint32_t y = 0; y < height; ++y) {
      for (uint32_t x = 0; x < width; ++x) {
        uint32_t srcIndex = (y * width + x) * channels;
        uint32_t dstX = y;
        uint32_t dstY = width - 1 - x;
        uint32_t dstIndex = (dstY * height + dstX) * channels;
        
        for (uint32_t c = 0; c < channels; ++c) {
          rotated[dstIndex + c] = pixels[srcIndex + c];
        }
      }
    }
  }
  
  return rotated;
}

std::vector<unsigned char> 
render::LayeredTexture::apply_tint(const std::vector<unsigned char> &pixels,
                                  uint32_t channels, const glm::vec4 &tint) {
  std::vector<unsigned char> tinted = pixels;
  
  for (size_t i = 0; i < tinted.size(); i += channels) {
    if (channels >= 3) {
      tinted[i + 0] = static_cast<unsigned char>(
        std::clamp(tinted[i + 0] * tint.r, 0.0f, 255.0f));
      tinted[i + 1] = static_cast<unsigned char>(
        std::clamp(tinted[i + 1] * tint.g, 0.0f, 255.0f));
      tinted[i + 2] = static_cast<unsigned char>(
        std::clamp(tinted[i + 2] * tint.b, 0.0f, 255.0f));
    }
    if (channels >= 4) {
      tinted[i + 3] = static_cast<unsigned char>(
        std::clamp(tinted[i + 3] * tint.a, 0.0f, 255.0f));
    }
  }
  
  return tinted;
}

void render::LayeredTexture::blend_layer(std::vector<unsigned char> &dst,
                                        const std::vector<unsigned char> &src,
                                        uint32_t width, uint32_t height) {
  // Alpha blending: dst = src * alpha + dst * (1 - alpha)
  constexpr uint32_t channels = 4; // Assuming RGBA
  
  for (uint32_t i = 0; i < width * height * channels; i += channels) {
    float srcAlpha = src[i + 3] / 255.0f;
    float dstAlpha = dst[i + 3] / 255.0f;
    
    // Pre-multiplied alpha blending
    float outAlpha = srcAlpha + dstAlpha * (1.0f - srcAlpha);
    
    if (outAlpha > 0.0f) {
      for (int c = 0; c < 3; ++c) {
        float srcColor = src[i + c] / 255.0f;
        float dstColor = dst[i + c] / 255.0f;
        float outColor = (srcColor * srcAlpha + dstColor * dstAlpha * (1.0f - srcAlpha)) / outAlpha;
        dst[i + c] = static_cast<unsigned char>(std::clamp(outColor * 255.0f, 0.0f, 255.0f));
      }
      dst[i + 3] = static_cast<unsigned char>(std::clamp(outAlpha * 255.0f, 0.0f, 255.0f));
    }
  }
}

bool render::LayeredTexture::composite_layers() {
  if (layers.empty()) {
    std::print(stderr, "LayeredTexture - {} - no layers to composite\n", identifier);
    return false;
  }
  
  // Determine the size of the composited image from the first layer
  uint32_t width = 0;
  uint32_t height = 0;
  
  for (auto &layer : layers) {
    if (layer.visible && layer.image) {
      width = std::max(width, layer.image->get_width());
      height = std::max(height, layer.image->get_height());
    }
  }
  
  if (width == 0 || height == 0) {
    std::print(stderr, "LayeredTexture - {} - invalid dimensions\n", identifier);
    return false;
  }
  
  // Create composited image buffer (RGBA)
  constexpr uint32_t channels = 4;
  std::vector<unsigned char> composited(width * height * channels);
  
  // Initialize with opaque white background for better visibility
  for (size_t i = 0; i < composited.size(); i += 4) {
    composited[i + 0] = 255; // R
    composited[i + 1] = 255; // G
    composited[i + 2] = 255; // B
    composited[i + 3] = 255; // A (opaque)
  }
  
  // Composite each visible layer
  for (auto &layer : layers) {
    if (!layer.visible || !layer.image) {
      continue;
    }
    
    // Get layer pixel data
    std::vector<unsigned char> layerPixels = layer.image->get_pixel_data();
    uint32_t layerWidth = layer.image->get_width();
    uint32_t layerHeight = layer.image->get_height();
    uint32_t layerChannels = layer.image->get_channels();
    
    // Apply rotation
    if (layer.rotation != 0.0f) {
      layerPixels = apply_rotation(layerPixels, layerWidth, layerHeight, 
                                   layerChannels, layer.rotation);
      // Swap dimensions if rotated 90 or 270 degrees
      int rot = static_cast<int>(layer.rotation) % 360;
      if (rot < 0) rot += 360;
      rot = ((rot + 45) / 90) * 90;
      if (rot == 90 || rot == 270) {
        std::swap(layerWidth, layerHeight);
      }
    }
    
    // Apply tint
    if (layer.tint != glm::vec4(1.0f)) {
      layerPixels = apply_tint(layerPixels, layerChannels, layer.tint);
    }
    
    // Convert to RGBA if needed
    std::vector<unsigned char> layerRGBA;
    if (layerChannels == 4) {
      layerRGBA = layerPixels;
    } else if (layerChannels == 3) {
      layerRGBA.resize(layerWidth * layerHeight * 4);
      for (size_t i = 0; i < layerWidth * layerHeight; ++i) {
        layerRGBA[i * 4 + 0] = layerPixels[i * 3 + 0];
        layerRGBA[i * 4 + 1] = layerPixels[i * 3 + 1];
        layerRGBA[i * 4 + 2] = layerPixels[i * 3 + 2];
        layerRGBA[i * 4 + 3] = 255; // Opaque
      }
    }
    
    // Resize layer to match composited size if needed
    if (layerWidth != width || layerHeight != height) {
      // For now, center the layer
      std::vector<unsigned char> resized(width * height * 4, 0);
      uint32_t offsetX = (width - layerWidth) / 2;
      uint32_t offsetY = (height - layerHeight) / 2;
      
      for (uint32_t y = 0; y < layerHeight && (y + offsetY) < height; ++y) {
        for (uint32_t x = 0; x < layerWidth && (x + offsetX) < width; ++x) {
          uint32_t srcIdx = (y * layerWidth + x) * 4;
          uint32_t dstIdx = ((y + offsetY) * width + (x + offsetX)) * 4;
          for (int c = 0; c < 4; ++c) {
            resized[dstIdx + c] = layerRGBA[srcIdx + c];
          }
        }
      }
      layerRGBA = resized;
    }
    
    // Blend this layer onto the composited image
    blend_layer(composited, layerRGBA, width, height);
  }
  
  // Create or update composited image
  if (!compositedImage) {
    Image::ImageCreateInfo imageInfo = {
      .identifier = identifier + "_composited",
      .format = vk::Format::eR8G8B8A8Srgb,
      .usage = vk::ImageUsageFlagBits::eTransferDst | 
               vk::ImageUsageFlagBits::eSampled
    };
    compositedImage = std::make_shared<Image>(logicalDevices, imageInfo);
  }
  
  // Load composited data into image
  if (!compositedImage->load_from_memory(composited.data(), width, height, channels)) {
    std::print(stderr, "LayeredTexture - {} - failed to load composited data\n", 
               identifier);
    return false;
  }
  
  std::print("LayeredTexture - {} - composited {} layers ({}x{})\n",
             identifier, layers.size(), width, height);
  
  return true;
}

bool render::LayeredTexture::load() {
  // Load all layer images in parallel using the thread pool
  std::vector<std::future<std::shared_ptr<Image>>> imageFutures;
  
  for (size_t i = 0; i < layers.size(); ++i) {
    if (!layers[i].imagePath.empty()) {
      auto future = device::Tasks::get_instance().add_task(
        [this, i]() -> std::shared_ptr<Image> {
          return load_or_get_cached_image(layers[i].imagePath);
        }
      );
      imageFutures.push_back(std::move(future));
    } else {
      imageFutures.push_back({});
    }
  }
  
  // Wait for all images to load and assign them
  for (size_t i = 0; i < imageFutures.size(); ++i) {
    if (imageFutures[i].valid()) {
      layers[i].image = imageFutures[i].get();
      if (!layers[i].image) {
        std::print(stderr, "LayeredTexture - {} - failed to load layer {} image {}\n",
                   identifier, i, layers[i].imagePath);
        return false;
      }
    }
  }
  
  // Composite layers
  if (!composite_layers()) {
    return false;
  }
  
  // Upload to GPU using the first logical device's thread
  if (!logicalDevices.empty() && compositedImage) {
    auto uploadFuture = std::async(std::launch::async, [this]() {
      return compositedImage->update_gpu_data();
    });
    
    if (!uploadFuture.get()) {
      std::print(stderr, "LayeredTexture - {} - failed to upload to GPU\n", 
                 identifier);
      return false;
    }
  }
  
  std::print("LayeredTexture - {} - loaded successfully\n", identifier);
  return true;
}

void render::LayeredTexture::add_layer(const ImageLayer &layer) {
  layers.push_back(layer);
  std::print("LayeredTexture - {} - added layer (total: {})\n", 
             identifier, layers.size());
}

void render::LayeredTexture::set_layer_tint(size_t layerIndex, const glm::vec4 &tint) {
  if (layerIndex < layers.size()) {
    layers[layerIndex].tint = tint;
  }
}

void render::LayeredTexture::set_layer_rotation(size_t layerIndex, float rotation) {
  if (layerIndex < layers.size()) {
    layers[layerIndex].rotation = rotation;
  }
}

void render::LayeredTexture::set_layer_visibility(size_t layerIndex, bool visible) {
  if (layerIndex < layers.size()) {
    layers[layerIndex].visible = visible;
  }
}

bool render::LayeredTexture::update_gpu() {
  if (compositedImage) {
    return compositedImage->update_gpu_data();
  }
  return false;
}

bool render::LayeredTexture::recomposite_and_update() {
  if (!composite_layers()) {
    return false;
  }
  return update_gpu();
}

const std::string &render::LayeredTexture::get_identifier() const {
  return identifier;
}

std::shared_ptr<render::Image> render::LayeredTexture::get_composited_image() const {
  return compositedImage;
}

size_t render::LayeredTexture::get_layer_count() const {
  return layers.size();
}

const render::LayeredTexture::ImageLayer &
render::LayeredTexture::get_layer(size_t index) const {
  return layers.at(index);
}

uint32_t render::LayeredTexture::get_width() const {
  return compositedImage ? compositedImage->get_width() : 0;
}

uint32_t render::LayeredTexture::get_height() const {
  return compositedImage ? compositedImage->get_height() : 0;
}
