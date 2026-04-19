#ifndef TEXTURE_TABLE_H_
#define TEXTURE_TABLE_H_

#include "bindless_types.h"
#include "device_export.h"
#include "vma_allocator.h"
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
  uint32_t firstLayer = 0; ///< Index into TextureLayer[]
  uint32_t layerCount = 0; ///< Number of layers to composite
  uint32_t flags = 0;      ///< Reserved / per-texture flags
  uint32_t _pad0 = 0;      ///< Padding to 16-byte alignment
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
  int32_t image2DIndex = -1;  ///< Index into images2D[],   -1 = none
  int32_t atlasIndex = -1;    ///< Index into atlases[],     -1 = none
  int32_t mapIndex = -1;      ///< Index into maps[],        -1 = none
  int32_t processingType = 0; ///< ProcessingType enum value (0 = none)

  // --- group 1 (vec4): atlas UV rect transform ---
  float atlasUvOffsetX = 0.0f;
  float atlasUvOffsetY = 0.0f;
  float atlasUvScaleX = 1.0f;
  float atlasUvScaleY = 1.0f;

  // --- group 2 (vec4): tint colour ---
  float tintR = 1.0f;
  float tintG = 1.0f;
  float tintB = 1.0f;
  float tintA = 1.0f;

  // --- group 3 (vec4): transform + blend mode ---
  float rotation = 0.0f; ///< Rotation in radians
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  uint32_t blendMode = 0; ///< BlendMode enum value (0 = alpha)

  // --- group 4 (vec4): processing parameters ---
  float procAmplitude = 0.0f;
  float procFrequency = 0.0f;
  float procPhase = 0.0f;
  float procSpeed = 0.0f;
};
static_assert(sizeof(GPUTextureLayer) == 80,
              "GPUTextureLayer must be 80 bytes (5 × vec4, std430)");

/**
 * @brief Per-effect record stored in the FaceEffectRecord SSBO
 *
 * One entry per effect — indexes into the flat GPUFaceEffectParam[]
 * array to locate the variable-length parameter list.
 */
struct DEVICE_API GPUFaceEffectRecord {
  uint32_t effectType = 0; ///< FaceEffectType enum value
  uint32_t firstParam = 0; ///< First index into GPUFaceEffectParam[]
  uint32_t paramCount = 0; ///< Number of params for this effect
  uint32_t _pad0 = 0;      ///< Padding to 16-byte alignment
};
static_assert(sizeof(GPUFaceEffectRecord) == 16,
              "GPUFaceEffectRecord must be 16 bytes (std430)");
static_assert(alignof(GPUFaceEffectRecord) == 4,
              "GPUFaceEffectRecord alignment must be 4");

/**
 * @brief Single float parameter stored in the FaceEffectParam SSBO
 *
 * Padded to 16 bytes for std430 vec4 alignment so that the buffer can
 * be indexed directly as a StructuredBuffer on the GPU side.
 */
struct DEVICE_API GPUFaceEffectParam {
  float value = 0.0f;
  float _pad0 = 0.0f;
  float _pad1 = 0.0f;
  float _pad2 = 0.0f;
};
static_assert(sizeof(GPUFaceEffectParam) == 16,
              "GPUFaceEffectParam must be 16 bytes (std430)");
static_assert(alignof(GPUFaceEffectParam) == 4,
              "GPUFaceEffectParam alignment must be 4");

/**
 * @brief CPU-side builder struct for adding effects via addEffect()
 *
 * Callers populate this and pass it to TextureTableManager::addEffect().
 * The manager converts it into a GPUFaceEffectRecord + GPUFaceEffectParam
 * entries for GPU upload.
 */
struct DEVICE_API FaceEffectEntry {
  FaceEffectType effect = FaceEffectType::eNone;
  uint32_t paramCount = 0;
  std::vector<float> params;

  FaceEffectEntry() = default;

  /**
   * @brief Construct with a type and a number of zero-initialized params.
   */
  FaceEffectEntry(FaceEffectType type, uint32_t count)
      : effect(type), paramCount(count), params(count, 0.0f) {}

  /**
   * @brief Construct with a type and explicit parameter values.
   */
  FaceEffectEntry(FaceEffectType type, std::initializer_list<float> p)
      : effect(type), paramCount(static_cast<uint32_t>(p.size())), params(p) {}

  /** @brief True when the effect type is not eNone */
  [[nodiscard]] bool isActive() const {
    return effect != FaceEffectType::eNone;
  }
};

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
   * @brief Reserve a new FaceEffectRecord and return its FaceEffectId
   *
   * Appends a GPUFaceEffectRecord and its GPUFaceEffectParam entries
   * into the flat params array. Follows the same pattern as addRecord().
   *
   * @param effect  CPU-side effect description with variable-length params
   * @return FaceEffectId  Stable index into the effect record table
   */
  FaceEffectId addEffect(const FaceEffectEntry &effect);

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

  /// @brief Get the GPU SSBO for face effect records (valid after
  /// uploadToGPU)
  [[nodiscard]] const AllocatedBuffer &getEffectRecordBuffer() const {
    return effectRecordBuffer_;
  }
  /// @brief Get the GPU SSBO for face effect params (valid after uploadToGPU)
  [[nodiscard]] const AllocatedBuffer &getEffectParamBuffer() const {
    return effectParamBuffer_;
  }

  [[nodiscard]] uint32_t getRecordCount() const;
  [[nodiscard]] uint32_t getLayerCount() const;
  [[nodiscard]] uint32_t getEffectRecordCount() const;
  [[nodiscard]] uint32_t getEffectParamCount() const;

  [[nodiscard]] bool isUploaded() const { return uploaded_; }

  void clear();

private:
  std::vector<GPUTextureRecord> records_;
  std::vector<GPUTextureLayer> layers_;
  std::vector<GPUFaceEffectRecord> effectRecords_;
  std::vector<GPUFaceEffectParam> effectParams_;

  AllocatedBuffer recordBuffer_;
  AllocatedBuffer layerBuffer_;
  AllocatedBuffer effectRecordBuffer_;
  AllocatedBuffer effectParamBuffer_;

  bool uploaded_ = false;
  mutable std::mutex tableMutex_;
};

} // namespace device

#endif // TEXTURE_TABLE_H_
