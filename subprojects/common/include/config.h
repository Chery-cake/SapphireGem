#pragma once

#include "vulkan/vulkan.hpp"
#include <cstdint>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>

namespace general {

class Config {

private:
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

#define PhysicalDeviceFeaturesList                                             \
  vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,             \
      vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features,  \
      vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT

  std::vector<const char *> instanceLayers;
  std::vector<const char *> instanceExtensions;
  std::vector<const char *> deviceLayers;
  std::vector<const char *> deviceExtensions;

  // Optional extensions that will be enabled if available
  std::vector<const char *> optionalInstanceExtensions;
  std::vector<const char *> optionalDeviceExtensions;

  uint32_t apiVersion;

  uint8_t maxFramesInFligth;

  bool reload;

  VmaVulkanFunctions vmaVulkanFunctions;
  bool vmaVulkanFunctionsInitialized;

  void initialize_vma_functions();

  Config();

public:
  ~Config();
  Config(Config &&) = delete;
  Config &operator=(Config &&) = delete;
  Config(const Config &) = delete;
  Config &operator=(const Config &) = delete;
  static Config &get_instance();

  vk::raii::DebugUtilsMessengerEXT
  set_up_debug_messanger(vk::raii::Instance &instance);

  static vk::StructureChain<PhysicalDeviceFeaturesList>
  get_features(vk::raii::PhysicalDevice physicalDevice = nullptr);

  bool validate_instance_requirements(const vk::raii::Context &context);
  bool validate_device_requirements(const vk::raii::PhysicalDevice &device);

  void check_and_enable_optional_instance_extensions(
      const vk::raii::Context &context);
  void check_and_enable_optional_device_extensions(
      const vk::raii::PhysicalDevice &device);

  void add_instance_layer(const char *layer);
  void remove_instance_layer(const char *layer);
  const std::vector<const char *> &get_instance_layer() const;

  void add_instance_extension(const char *extension);
  void remove_instance_extension(const char *extension);
  const std::vector<const char *> &get_instance_extension() const;

  void add_device_layer(const char *layer);
  void remove_device_layer(const char *layer);
  const std::vector<const char *> &get_device_layer() const;

  void add_device_extension(const char *extension);
  void remove_device_extension(const char *extension);
  const std::vector<const char *> &get_device_extension() const;

  void add_optional_instance_extension(const char *extension);
  void add_optional_device_extension(const char *extension);

  void set_api_version(uint32_t &version);
  const uint32_t get_api_version() const;

  void set_max_frames(uint8_t &max);
  const uint8_t &get_max_frames() const;

  const VmaVulkanFunctions *get_vma_vulkan_functions();

  bool needs_reload() const;
  void mark_reload_complete();
};

} // namespace general
