#pragma once

#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

namespace device {

class LogicalDevice;

class SwapChain {
private:
  LogicalDevice *logicalDevice;

  GLFWwindow *window;
  vk::raii::SurfaceKHR *surface;

  // swap chain with presentation
  vk::raii::SwapchainKHR swapChain;
  std::vector<vk::Image> swapChainImages;
  std::vector<vk::raii::ImageView> swapChainImageViews;

  // swap chain without presentation
  vk::raii::Image image;
  vk::raii::ImageView imageView;
  vk::raii::DeviceMemory imageMemory;

  // depth buffer
  vk::raii::Image depthImage;
  vk::raii::ImageView depthImageView;
  vk::raii::DeviceMemory depthImageMemory;
  vk::Format depthFormat;

  vk::SurfaceFormatKHR surfaceFormat;
  vk::Extent2D extent2D;

  void set_surface_format(std::vector<vk::SurfaceFormatKHR> availableFormats);
  void create_depth_resources();
  void destroy_depth_resources();

public:
  SwapChain(LogicalDevice *logicalDevice, GLFWwindow *window,
            vk::raii::SurfaceKHR &surface);
  SwapChain(LogicalDevice *logicalDevice, vk::SurfaceFormatKHR format,
            vk::Extent2D extent2D);
  ~SwapChain();

  void create_swap_chain();
  void clear_swap_chain();
  void create_swap_image_views();

  void recreate_swap_chain();
  void recreate_swap_chain(vk::SurfaceFormatKHR format, vk::Extent2D extent);

  // Frame rendering helpers
  vk::ResultValue<uint32_t>
  acquire_next_image(const vk::raii::Semaphore &semaphore);
  void transition_image_for_rendering(vk::raii::CommandBuffer &commandBuffer,
                                      uint32_t imageIndex);
  void transition_image_for_present(vk::raii::CommandBuffer &commandBuffer,
                                    uint32_t imageIndex);
  void begin_rendering(vk::raii::CommandBuffer &commandBuffer,
                       uint32_t imageIndex);
  void end_rendering(vk::raii::CommandBuffer &commandBuffer);

  vk::SurfaceFormatKHR get_surface_format();
  vk::Extent2D get_extent2D();
  vk::raii::SwapchainKHR &get_swap_chain();
  const std::vector<vk::Image> &get_images() const;
  const std::vector<vk::raii::ImageView> &get_image_views() const;
};

} // namespace device
