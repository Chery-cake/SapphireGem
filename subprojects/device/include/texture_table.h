#ifndef TEXTURE_TABLE_H_
#define TEXTURE_TABLE_H_

#include "bindless_types.h"
#include "device_export.h"
#include "vma_allocator.h"
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>

namespace device {

// ============================================================================
// GPU-facing structs – must match the SSBO layouts in bindless_common.slang
// All fields use std430 packing (natural alignment).
// ============================================================================

/**
 * @brief Per-texture record stored in the TextureRecord SSBO
 *
 * The shader indexes this by TextureId to find out which layers to
 * composite.
 */
struct DEVICE_API GPUTextureRecord {
  uint32_t firstLayer; ///< Index into TextureLayer[]
  uint32_t layerCount; ///< Number of layers to composite
  uint32_t flags;      ///< Reserved / per-texture flags
  uint32_t _pad0;      ///< Padding to 16-byte alignment
};
static_assert(sizeof(GPUTextureRecord) == 16,
              "GPUTextureRecord must be 16 bytes (std430)");
static_assert(alignof(GPUTextureRecord) == 4,
              "GPUTextureRecord alignment must be 4");

/**
 * @brief Per-layer record stored in the TextureLayer SSBO
 *
 * Five vec4-sized groups for clean GPU cache-line usage.
 */
struct DEVICE_API GPUTextureLayer {
  // --- group 0 (ivec4): image references ---
  int32_t image2DIndex;   ///< Index into images2D[],   -1 = none
  int32_t atlasIndex;     ///< Index into atlases[],     -1 = none
  int32_t mapIndex;       ///< Index into maps[],        -1 = none
  int32_t processingType; ///< ProcessingType enum value

  // --- group 1 (vec4): atlas UV rect transform ---
  float atlasUvOffsetX;
  float atlasUvOffsetY;
  float atlasUvScaleX;
  float atlasUvScaleY;

  // --- group 2 (vec4): tint colour ---
  float tintR;
  float tintG;
  float tintB;
  float tintA;

  // --- group 3 (vec4): transform + blend mode ---
  float rotation; ///< Rotation in radians
  float scaleX;
  float scaleY;
  uint32_t blendMode; ///< BlendMode enum value

  // --- group 4 (vec4): processing parameters ---
  float procAmplitude;
  float procFrequency;
  float procPhase;
  float procSpeed;
};
static_assert(sizeof(GPUTextureLayer) == 80,
              "GPUTextureLayer must be 80 bytes (5 × vec4, std430)");

// ============================================================================
// CPU-side builder / manager
// ============================================================================

/**
 * @brief Manages CPU-side texture records and layers, and uploads them
 *        into device-local SSBOs via VMA staging buffers.
 *
 * Thread-safe: registration may happen from worker threads.
 * GPU uploads must be externally synchronised with the render loop.
 *
 * Typical flow:
 *   1. addRecord() / addLayers()   – from any thread
 *   2. uploadToGPU()               – from the GPU/render thread
 *   3. getRecordBuffer() / getLayerBuffer()  – bind before draw
 */
class DEVICE_API TextureTableManager {
public:
  TextureTableManager() = default;
  ~TextureTableManager() = default;

  TextureTableManager(const TextureTableManager &) = delete;
  TextureTableManager &operator=(const TextureTableManager &) = delete;

  /**
   * @brief Reserve a new TextureRecord and return its TextureId
   *
   * The caller should subsequently call addLayers() to populate
   * the layers that this record references.
   *
   * @param layerCount  Number of layers this texture will have
   * @return TextureId  Stable index into the record table
   */
  TextureId addRecord(uint32_t layerCount);

  /**
   * @brief Append layers for a previously added record
   *
   * Must be called exactly once per addRecord(), with exactly
   * `layerCount` entries.
   *
   * @param id      TextureId returned by addRecord()
   * @param layers  Layer data (must have record.layerCount entries)
   */
  void setLayers(TextureId id, const std::vector<GPUTextureLayer> &layers);

  /**
   * @brief Upload the current CPU tables into device-local SSBOs
   *
   * Uses a staging buffer for the transfer.  The caller must ensure
   * this is not called concurrently with draws that read the buffers.
   *
   * @param allocator  VMA allocator (per-device)
   * @param device     GPU device for command submission
   * @return true on success
   */
  bool uploadToGPU(VMAAllocator &allocator, GPUDevice &device);

  // --- Accessors (valid after uploadToGPU) ---

  [[nodiscard]] const AllocatedBuffer &getRecordBuffer() const {
    return recordBuffer_;
  }
  [[nodiscard]] const AllocatedBuffer &getLayerBuffer() const {
    return layerBuffer_;
  }

  [[nodiscard]] uint32_t getRecordCount() const;
  [[nodiscard]] uint32_t getLayerCount() const;

  [[nodiscard]] bool isUploaded() const { return uploaded_; }

  void clear();

private:
  std::vector<GPUTextureRecord> records_;
  std::vector<GPUTextureLayer> layers_;

  AllocatedBuffer recordBuffer_;
  AllocatedBuffer layerBuffer_;

  bool uploaded_ = false;
  mutable std::mutex tableMutex_;
};

} // namespace device

#endif // TEXTURE_TABLE_H_
