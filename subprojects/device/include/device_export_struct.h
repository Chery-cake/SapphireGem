#ifndef DEVICE_EXPORT_STRUCT_H_
#define DEVICE_EXPORT_STRUCT_H_
#ifdef ENGINE_DEBUG

#include "shader_manager.h"
#include "vma_allocator.h"
#include "vulkan_device.h"
#include "vulkan_instance.h"

struct deviceState {
  device::VulkanInstance *instance;
  device::DeviceManager *deviceManager;
  device::VMAManager *vmaManager;
  device::ShaderManager *shaderManager;
};

#endif
#endif // DEVICE_EXPORT_STRUCT_H_
