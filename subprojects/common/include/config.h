#pragma once

#include <vector>

#ifdef __INTELLISENSE__
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

class Config {

private:
#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

#define PhysicalDeviceFeaturesList                                             \
  vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan14Features,             \
      vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,                       \
      vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features

  std::vector<const char *> instanceLayers;
  std::vector<const char *> instanceExtensions;
  std::vector<const char *> deviceLayers;
  std::vector<const char *> deviceExtensions;

  bool reload;

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

  bool validate_instance_requirements(const vk::raii::Context &context) const;
  bool
  validate_device_requirements(const vk::raii::PhysicalDevice &device) const;

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
};
