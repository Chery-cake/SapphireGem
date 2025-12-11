#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include "render_strategy.h"
#include "renderable_object.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// 3D Engine for managing renderable objects and rendering strategies
class Engine3D {
private:
  // Managers (not owned, provided by Renderer)
  DeviceManager *deviceManager;
  MaterialManager *materialManager;
  BufferManager *bufferManager;

  // Rendering strategy configuration
  MultiGPUConfig gpuConfig;

  // Renderable objects
  std::unordered_map<std::string, std::unique_ptr<RenderableObject>> objects;
  std::vector<RenderableObject *> renderQueue; // Sorted for efficient rendering

  // Material instance tracking (for shared materials)
  std::unordered_map<std::string, uint32_t>
      materialUsageCount; // Track how many objects use each material

  void rebuild_render_queue();
  void sort_render_queue_by_material();

public:
  Engine3D(DeviceManager *devMgr, MaterialManager *matMgr,
           BufferManager *bufMgr);
  ~Engine3D();

  // Configuration
  void set_render_strategy(RenderStrategy strategy);
  void set_gpu_config(const MultiGPUConfig &config);
  const MultiGPUConfig &get_gpu_config() const { return gpuConfig; }

  // Object management
  RenderableObject *create_object(const RenderableObject::CreateInfo &createInfo);
  void remove_object(const std::string &identifier);
  RenderableObject *get_object(const std::string &identifier);

  // Rendering
  void render_all_objects(vk::raii::CommandBuffer &commandBuffer,
                          uint32_t deviceIndex, uint32_t frameIndex);

  // Get statistics
  size_t get_object_count() const { return objects.size(); }
  size_t get_material_count() const { return materialUsageCount.size(); }
};
