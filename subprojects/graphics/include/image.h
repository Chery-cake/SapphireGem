#pragma once

#include "logical_device.h"
#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <glm/ext/vector_float4.hpp>
#include <string>

class Image {
public:
  struct ImageCreateInfo {
    std::string identifier;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 4; // RGBA by default
    vk::Format format = vk::Format::eR8G8B8A8Srgb;
    vk::ImageUsageFlags usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
    vk::Filter filter = vk::Filter::eLinear;
    vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat;
    bool generateMipmaps = false;
  };

  struct ImageResources {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    vk::raii::ImageView imageView{nullptr};
    vk::raii::Sampler sampler{nullptr};
    std::vector<vk::raii::DescriptorSet> descriptorSets;
  };

private:
  mutable std::mutex imageMutex;

  std::string identifier;
  uint32_t width;
  uint32_t height;
  uint32_t channels;
  uint32_t mipLevels;
  vk::Format format;

  std::vector<unsigned char> pixelData;

  std::vector<LogicalDevice *> logicalDevices;
  std::vector<std::unique_ptr<ImageResources>> deviceResources;

  bool create_image(LogicalDevice *device, ImageResources &resources,
                    const ImageCreateInfo &createInfo);
  bool create_image_view(LogicalDevice *device, ImageResources &resources,
                         const ImageCreateInfo &createInfo);
  bool create_sampler(LogicalDevice *device, ImageResources &resources,
                      const ImageCreateInfo &createInfo);
  bool upload_data(LogicalDevice *device, ImageResources &resources,
                   const void *data, uint32_t dataSize);
  void destroy_image(LogicalDevice *device, ImageResources &resources);

  void apply_color_tint(const glm::vec4 &tint);
  void rotate_image_90(bool clockwise);

public:
  Image(const std::vector<LogicalDevice *> &devices,
        const ImageCreateInfo &createInfo);
  ~Image();

  // Load image from file
  bool load_from_file(const std::string &filepath);

  // Load image from memory
  bool load_from_memory(const unsigned char *data, uint32_t width,
                        uint32_t height, uint32_t channels);

  // Image manipulation
  void set_color_tint(const glm::vec4 &tint);
  void rotate_90_clockwise();
  void rotate_90_counter_clockwise();
  void rotate_180();

  // Upload modified data to GPU
  bool update_gpu_data();

  // Getters
  const std::string &get_identifier() const;
  uint32_t get_width() const;
  uint32_t get_height() const;
  uint32_t get_channels() const;
  vk::Format get_format() const;
  const std::vector<unsigned char> &get_pixel_data() const;

  VkImage get_image(uint32_t deviceIndex = 0) const;
  vk::raii::ImageView &get_image_view(uint32_t deviceIndex = 0);
  vk::raii::Sampler &get_sampler(uint32_t deviceIndex = 0);
};
