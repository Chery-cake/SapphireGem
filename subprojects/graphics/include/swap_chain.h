#pragma once

#include "vulkan/vulkan.hpp"
#include <GLFW/glfw3.h>
#include <vector>
#include <vulkan/vulkan_raii.hpp>

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

  vk::SurfaceFormatKHR surfaceFormat;
  vk::Extent2D extent2D;

  void set_surface_format(std::vector<vk::SurfaceFormatKHR> availableFormats);

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

  vk::SurfaceFormatKHR get_surface_format();
  vk::Extent2D get_extent2D();
  vk::raii::SwapchainKHR &get_swap_chain();
  const std::vector<vk::Image> &get_images() const;
  const std::vector<vk::raii::ImageView> &get_image_views() const;

  // Frame rendering helpers
  std::pair<vk::Result, uint32_t>
  acquire_next_image(const vk::raii::Semaphore &semaphore);
  void transition_image_for_rendering(vk::raii::CommandBuffer &commandBuffer,
                                       uint32_t imageIndex);
  void transition_image_for_present(vk::raii::CommandBuffer &commandBuffer,
                                     uint32_t imageIndex);
  void begin_rendering(vk::raii::CommandBuffer &commandBuffer,
                       uint32_t imageIndex);
  void end_rendering(vk::raii::CommandBuffer &commandBuffer);
};
