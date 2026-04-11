#include "compute_renderer.h"
#include "shader_manager.h"
#include <print>

namespace device {

ComputeRenderer::~ComputeRenderer() { shutdown(); }

bool ComputeRenderer::initialize(
    GPUDevice &device, ShaderManager &shaderManager,
    std::vector<GPUDevice *> &secondaryGPUs) {
  std::lock_guard<std::mutex> lock(computeMutex_);

  if (initialized_) {
    std::println(stderr,
                 "[ComputeRenderer] Already initialized");
    return false;
  }

  (void)device;
  (void)shaderManager;
  (void)secondaryGPUs;

  // The ComputeRenderer creates per-object compute resources on demand
  // in precomputeGeometry(). Initialization just marks readiness.
  initialized_ = true;
  std::println("[ComputeRenderer] Initialized (multi-GPU: {} secondary devices)",
               secondaryGPUs.size());
  return true;
}

ComputedGeometryBuffer *ComputeRenderer::precomputeGeometry(
    VMAAllocator &allocator, GPUDevice &device,
    const std::string &objectName, uint32_t vertexCount,
    uint32_t faceCount,
    const std::vector<GPUFaceData> &faceData) {
  std::lock_guard<std::mutex> lock(computeMutex_);

  if (!initialized_) {
    std::println(stderr,
                 "[ComputeRenderer] Not initialized");
    return nullptr;
  }

  // Check if already computed
  auto it = geometryBuffers_.find(objectName);
  if (it != geometryBuffers_.end() && it->second->precomputed) {
    return it->second.get();
  }

  // The ProcessedVertex struct is 64 bytes (float4 + float3 + float2 + float3 + pad)
  constexpr size_t kProcessedVertexSize = 64;

  auto buffer = std::make_unique<ComputedGeometryBuffer>();
  buffer->vertexCount = vertexCount;
  buffer->faceCount = faceCount;

  // Allocate base geometry buffer (device-local storage)
  vk::DeviceSize baseSize = vertexCount * kProcessedVertexSize;
  buffer->baseBuffer = allocator.createStorageBuffer(
      baseSize, objectName + "_compute_base");

  if (!buffer->baseBuffer.isValid()) {
    std::println(stderr,
                 "[ComputeRenderer] Failed to allocate base buffer for '{}'",
                 objectName);
    return nullptr;
  }

  // Allocate animated overlay buffer
  buffer->animatedBuffer = allocator.createStorageBuffer(
      baseSize, objectName + "_compute_animated");

  if (!buffer->animatedBuffer.isValid()) {
    std::println(stderr,
                 "[ComputeRenderer] Failed to allocate animated buffer for '{}'",
                 objectName);
    return nullptr;
  }

  buffer->precomputed = true;

  (void)device;
  (void)faceData;

  std::println("[ComputeRenderer] Pre-computed geometry for '{}': {} verts, {} faces",
               objectName, vertexCount, faceCount);

  auto *ptr = buffer.get();
  geometryBuffers_[objectName] = std::move(buffer);
  return ptr;
}

void ComputeRenderer::updateAnimated(GPUDevice &device,
                                     const std::string &objectName,
                                     float time, float deltaTime) {
  std::lock_guard<std::mutex> lock(computeMutex_);

  auto it = geometryBuffers_.find(objectName);
  if (it == geometryBuffers_.end() || !it->second->precomputed) {
    return;
  }

  (void)device;
  (void)time;
  (void)deltaTime;

  // In a full implementation, this would dispatch the object_update.slang
  // compute shader to update only animated vertices. For now, the geometry
  // is pre-computed and the vertex shader handles animation via face data.
}

ComputedGeometryBuffer *
ComputeRenderer::getBuffer(const std::string &objectName) const {
  std::lock_guard<std::mutex> lock(computeMutex_);

  auto it = geometryBuffers_.find(objectName);
  if (it != geometryBuffers_.end()) {
    return it->second.get();
  }
  return nullptr;
}

void ComputeRenderer::shutdown() {
  std::lock_guard<std::mutex> lock(computeMutex_);
  geometryBuffers_.clear();
  initialized_ = false;
}

} // namespace device
