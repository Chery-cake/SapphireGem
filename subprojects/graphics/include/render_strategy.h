#pragma once

#include <cstdint>

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
