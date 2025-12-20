#pragma once

#include "buffer_manager.h"
#include "device_manager.h"
#include "material_manager.h"
#include "object.h"
#include "texture_manager.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace render {

class ObjectManager {
public:
  // Multi-GPU rendering strategies
  enum class RenderStrategy {
    // Single GPU rendering (default)
    SINGLE_GPU,

    // Alternate Frame Rendering - each GPU renders different frames
    AFR,

    // Split Frame Rendering - frame divided spatially across GPUs
    SFR,

    // Hybrid - mix of AFR and SFR based on workload
    HYBRID,

    // Multi-Queue with Resource Streaming - advanced scheduling
    MULTI_QUEUE_STREAMING
  };

  // Configuration for multi-GPU rendering
  struct MultiGPUConfig {
    RenderStrategy strategy = RenderStrategy::AFR;

    // For SFR: how to split the frame
    enum class SplitMode {
      HORIZONTAL,  // Split horizontally
      VERTICAL,    // Split vertically
      CHECKERBOARD // Alternate tiles
    } splitMode = SplitMode::HORIZONTAL;

    // For Hybrid: threshold for switching strategies
    float workloadThreshold = 0.7f;

    // Enable VkDeviceGroup features if available
    bool useDeviceGroup = true;

    // Resource streaming configuration
    uint32_t streamingBufferCount = 3;
    bool enableAsyncTransfer = true;

    // Multi-queue support
    bool useMultiQueue = true;
    uint32_t transferQueueCount = 1;
    uint32_t computeQueueCount = 1;
  };

private:
  device::DeviceManager *deviceManager;
  MaterialManager *materialManager;
  device::BufferManager *bufferManager;
  class TextureManager *textureManager;

  MultiGPUConfig gpuConfig;

  std::unordered_map<std::string, std::unique_ptr<Object>> objects;
  std::vector<Object *> renderQueue;

  // Material instance tracking (for shared materials)
  std::unordered_map<std::string, uint32_t>
      materialUsageCount; // Track how many objects use each material

  void rebuild_render_queue();
  void sort_render_queue_by_material();

public:
  ObjectManager(device::DeviceManager *deviceManager,
                MaterialManager *materialManager,
                device::BufferManager *bufferManager,
                TextureManager *textureManager);
  ~ObjectManager();

  // Configuration
  void set_render_strategy(RenderStrategy strategy);
  void set_gpu_config(const MultiGPUConfig &config);
  const MultiGPUConfig &get_gpu_config() const;

  // Object management
  Object *create_object(const Object::ObjectCreateInfo &createInfo);

  void remove_object(const std::string &identifier);
  Object *get_object(const std::string &identifier);

  // Rendering
  void render_all_objects(vk::raii::CommandBuffer &commandBuffer,
                          uint32_t deviceIndex, uint32_t frameIndex);

  // Get statistics
  size_t get_object_count() const;
  size_t get_material_count() const;
};

} // namespace render
