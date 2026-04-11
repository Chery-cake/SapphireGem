#ifndef COMPUTE_RENDERER_H_
#define COMPUTE_RENDERER_H_

#include "bindless_types.h"
#include "device_export.h"
#include "resource_registry.h"
#include "vma_allocator.h"
#include "vulkan/vulkan.hpp"
#include "vulkan/vulkan_raii.hpp"
#include "vulkan_device.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace device {

class ShaderManager;

/**
 * @brief Pre-computed geometry buffer for an object
 *
 * Holds the device-local storage buffers produced by the compute
 * pre-computation pass. The buffer stores object-space positions,
 * normals, UVs, and colours so that the vertex shader can simply
 * output clip-space positions.
 */
struct DEVICE_API ComputedGeometryBuffer {
  AllocatedBuffer baseBuffer;     ///< Pre-computed (static) geometry
  AllocatedBuffer animatedBuffer; ///< Per-frame animated overlay
  uint32_t vertexCount = 0;
  uint32_t faceCount = 0;
  bool precomputed = false;

  [[nodiscard]] bool isValid() const { return baseBuffer.isValid(); }
};

/**
 * @brief Compute-shader-based rendering subsystem
 *
 * Objects and surfaces can be defined, computed, and stored in GPU
 * memory by compute shaders so that static geometry does not need to
 * be recalculated every frame. Only dynamic/animated parts re-run
 * the compute pass.
 *
 * Usage:
 *   1. Call precomputeGeometry() once at object load time.
 *   2. Call updateAnimated() each frame for objects with animated faces.
 *   3. The vertex shader reads from the pre-computed SSBO.
 *
 * Multi-GPU: if secondary GPUs are available, the pre-computation
 * pass can be dispatched on a secondary GPU's compute queue while
 * the primary GPU handles the render pass.
 *
 * Thread safety: dispatch of compute commands goes through the
 * existing "gpu" thread pool (ThreadManager::instance().getPool("gpu")).
 */
class DEVICE_API ComputeRenderer {
public:
  ComputeRenderer() = default;
  ~ComputeRenderer();

  // Disable copy
  ComputeRenderer(const ComputeRenderer &) = delete;
  ComputeRenderer &operator=(const ComputeRenderer &) = delete;

  /**
   * @brief Initialize the compute renderer
   *
   * Creates the compute pipeline, descriptor set layout, and
   * command pool for compute dispatch.
   *
   * @param device Primary GPU device
   * @param shaderManager Shader manager for compute shader compilation
   * @param secondaryGPUs Secondary GPUs for multi-GPU compute dispatch
   * @return true if initialization succeeded
   */
  bool initialize(GPUDevice &device, ShaderManager &shaderManager,
                  std::vector<GPUDevice *> &secondaryGPUs);

  /**
   * @brief Pre-compute geometry for an object (runs once at load time)
   *
   * Dispatches a compute shader that processes raw vertex data and
   * writes transformed, lighting-pre-baked ProcessedVertex[] into
   * device-local memory.
   *
   * @param allocator VMA allocator for buffer creation
   * @param device GPU device
   * @param objectName Name of the object (for buffer naming)
   * @param vertexCount Number of vertices
   * @param faceCount Number of faces
   * @param faceData Per-face material data
   * @return Pointer to the computed geometry buffer
   */
  ComputedGeometryBuffer *
  precomputeGeometry(VMAAllocator &allocator, GPUDevice &device,
                     const std::string &objectName, uint32_t vertexCount,
                     uint32_t faceCount,
                     const std::vector<GPUFaceData> &faceData);

  /**
   * @brief Update animated vertices for a single frame
   *
   * Only re-processes vertices belonging to faces with effectFlags != 0.
   *
   * @param device GPU device
   * @param objectName Name of the object
   * @param time Current time for animation
   * @param deltaTime Time since last frame
   */
  void updateAnimated(GPUDevice &device, const std::string &objectName,
                      float time, float deltaTime);

  /**
   * @brief Get the computed geometry buffer for an object
   * @param objectName Object name
   * @return Pointer to buffer, or nullptr if not found
   */
  [[nodiscard]] ComputedGeometryBuffer *
  getBuffer(const std::string &objectName) const;

  /**
   * @brief Release all resources
   */
  void shutdown();

  [[nodiscard]] bool isInitialized() const { return initialized_; }

private:
  bool initialized_ = false;
  std::unordered_map<std::string, std::unique_ptr<ComputedGeometryBuffer>>
      geometryBuffers_;
  mutable std::mutex computeMutex_;
};

} // namespace device

#endif // COMPUTE_RENDERER_H_
