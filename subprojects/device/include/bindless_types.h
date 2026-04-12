#ifndef BINDLESS_TYPES_H_
#define BINDLESS_TYPES_H_

#include "device_export.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <print>
#include <ranges>
#include <vector>

namespace device {

/**
 * @brief Handle identifying a texture record in the GPU texture table
 *
 * At draw time the shader uses this integer to fetch the TextureRecord
 * from the SSBO, then iterates layers. The value is a direct index
 * into the TextureRecord[] storage buffer.
 */
struct DEVICE_API TextureId {
  uint32_t index = INVALID;
  static constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();

  [[nodiscard]] bool isValid() const { return index != INVALID; }

  bool operator==(const TextureId &other) const { return index == other.index; }
  bool operator!=(const TextureId &other) const { return index != other.index; }
};

/**
 * @brief Handle into one of the per-kind image arrays maintained by
 *        ImageArrayRegistry
 *
 * Stores both the descriptor-array index and the image-kind so the
 * registry can resolve it to the correct descriptor set binding.
 */
struct DEVICE_API ImageHandle {
  uint32_t index = INVALID;
  static constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();

  [[nodiscard]] bool isValid() const { return index != INVALID; }

  bool operator==(const ImageHandle &other) const {
    return index == other.index;
  }
  bool operator!=(const ImageHandle &other) const {
    return index != other.index;
  }
};

/**
 * @brief Discriminator for the separate per-kind image arrays
 *
 * Each kind maps to its own descriptor array binding inside the
 * global bindless descriptor set.  Extensible: add eCubeMap, e3D, …
 * later without breaking the existing entries.
 */
enum class ImageKind : uint32_t {
  eImage2D = 0, ///< General colour / albedo images
  eAtlas = 1,   ///< Atlas source images
  eMap = 2,     ///< Normal / roughness / metalness / AO maps
  eCount        ///< Sentinel – number of kinds
};

/**
 * @brief Blend mode used when compositing texture layers
 */
enum class BlendMode : uint32_t {
  eAlpha = 0,    ///< Standard alpha blending
  eAdditive = 1, ///< Additive blending
  eMultiply = 2, ///< Multiplicative blending
  eCount
};

/**
 * @brief Processing type for procedural effects on a texture layer
 *
 * Designed so that compute-based pre-processing can later replace
 * the procedural path: the layer simply switches to referencing a
 * pre-computed image handle instead.
 */
enum class ProcessingType : uint32_t {
  eNone = 0, ///< No processing – sample image directly
  eWave = 1, ///< Sine-wave UV displacement
  eCount
};

/**
 * @brief Push constant data for bindless shaders
 *
 * Matches the PushData struct in bindless shader files.
 * Used by Object::draw() to push time + objectId + counts for compute.
 *
 * Wave parameters are stored per-face in FaceMaterial effects and
 * uploaded via the FaceData SSBO — they are NOT in push constants.
 * This avoids combinatorial parameter growth as more effects are added.
 *
 * vertexCount and indexCount are used by the compute shader to guard
 * dispatch bounds and iterate the index buffer for adjacency scanning.
 */
struct DEVICE_API BindlessPushConstants {
  float time = 0.0f;
  uint32_t objectId = 0;
  uint32_t vertexCount = 0;
  uint32_t indexCount = 0;
};
// 16 bytes = Vulkan push constant alignment; matches PushData in shaders
static_assert(sizeof(BindlessPushConstants) == 16,
              "BindlessPushConstants must be 16 bytes");

// ============================================================================
// Effect system: array-based effects with type + parameters
// ============================================================================

/**
 * @brief Effect type enumeration
 *
 * Each value represents a distinct per-face rendering effect.
 * New effects can be added by extending this enum and implementing
 * the corresponding shader logic in face_effects.slang.
 */
enum class EffectType : uint32_t {
  eNone = 0,
  eGradient = 1,
  eWave = 2,
  eDrawing = 3,
  eCount
};

/**
 * @brief Effect type enumeration for the GPU face-effect SSBO system
 *
 * Distinct from EffectType (which drives the per-face bitmask path).
 * Each value maps to a shader branch in the FaceEffectRecord dispatch.
 */
enum class FaceEffectType : uint32_t {
  eNone = 0,
  eWave = 1,
  eRipple = 2,
  eCount
};

/**
 * @brief Handle identifying a face-effect record in the GPU effect table
 *
 * Analogous to TextureId: the value is a direct index into the
 * GPUFaceEffectRecord[] storage buffer.
 */
struct DEVICE_API FaceEffectId {
  uint32_t index = INVALID;
  static constexpr uint32_t INVALID = std::numeric_limits<uint32_t>::max();

  [[nodiscard]] bool isValid() const { return index != INVALID; }

  bool operator==(const FaceEffectId &other) const {
    return index == other.index;
  }
  bool operator!=(const FaceEffectId &other) const {
    return index != other.index;
  }
};

/**
 * @brief Number of parameters required by each effect type.
 *
 * Used to validate FaceEffect construction and interpret the params array.
 */
[[nodiscard]] constexpr uint32_t
faceEffectParamCount(EffectType type) noexcept {
  switch (type) {
  case EffectType::eNone:
    return 0;
  case EffectType::eGradient:
    return 2; // param[0]=range, param[1]=angle
  case EffectType::eWave:
    return 2; // param[0]=amplitude, param[1]=frequency
  case EffectType::eDrawing:
    return 0; // no params
  default:
    return 0;
  }
}

/**
 * @brief A single effect entry with type and a variable-length parameter list.
 *
 * The number of meaningful parameters is determined by
 * faceEffectParamCount(type).  The vector may hold more values than
 * the effect strictly requires — the GPU will only read what it needs.
 */
struct DEVICE_API FaceEffect {
  EffectType type = EffectType::eNone;
  std::vector<float> params;

  FaceEffect() = default;

  /**
   * @brief Construct a FaceEffect with just a type (zero-param effects).
   */
  explicit FaceEffect(EffectType t) : type(t) {}

  /**
   * @brief Convenience constructor for effects with exactly 2 params.
   *
   * If the effect type does not take 2 params, a warning is logged
   * and the extra values are stored but may be ignored on the GPU.
   */
  FaceEffect(EffectType t, float p0, float p1) : type(t), params{p0, p1} {
    if (faceEffectParamCount(t) != 2) {
      std::println(stderr,
                   "[FaceEffect] Warning: effect type {} expects {} params, "
                   "but 2 were provided — GPU may ignore extras",
                   static_cast<uint32_t>(t), faceEffectParamCount(t));
    }
  }

  [[nodiscard]] bool isActive() const { return type != EffectType::eNone; }
};

/// Maximum number of simultaneous effects per face
inline constexpr uint32_t MAX_FACE_EFFECTS = 4;

/**
 * @brief Unified per-face material description
 *
 * Used on the CPU side (Object face material storage). The effects
 * array provides a structured way to manage effects. For GPU upload,
 * convert to GPUFaceData via GPUFaceData::fromFaceMaterial().
 */
struct DEVICE_API FaceMaterial {
  // --- GPU-relevant fields ---
  uint32_t textureId = TextureId::INVALID; ///< TextureRecord index

  // --- CPU-side structured effects ---
  std::array<FaceEffect, MAX_FACE_EFFECTS> effects{};

  /**
   * @brief Add an effect to the first available slot
   * @return true if the effect was added, false if all slots are full
   */
  [[nodiscard]] bool addEffect(FaceEffect fx) {
    for (auto &slot : effects) {
      if (!slot.isActive()) {
        slot = std::move(fx);
        return true;
      }
    }
    return false; // All slots full
  }

  /**
   * @brief Remove the first effect matching the given type
   * @return true if an effect was removed
   */
  bool removeEffect(EffectType type) {
    for (auto &slot : effects) {
      if (slot.type == type) {
        slot = {};
        return true;
      }
    }
    return false;
  }

  /**
   * @brief Clear all effects
   */
  void clearEffects() { effects = {}; }

  /**
   * @brief Check if any effect is active
   */
  [[nodiscard]] bool hasEffects() const {
    for (const auto &fx : effects) {
      if (fx.isActive())
        return true;
    }
    return false;
  }
};

/**
 * @brief GPU-side per-face data (16 bytes, std430)
 *
 * This is the plain-old-data struct that is uploaded to the GPU SSBO.
 * Extracted from FaceMaterial for GPU upload.
 *
 * The effectFlags bitmask is built from the FaceMaterial effects array:
 *   bit 0 (0x01): gradient
 *   bit 1 (0x02): wave
 *   bit 2 (0x04): drawing
 *
 * There are only 2 float param slots (effectParam0, effectParam1) for all
 * effects combined on a face. Params are packed in effect-slot order: the
 * first parameterized effect fills as many slots as it needs (up to 2),
 * then the second parameterized effect takes whatever remains. If both
 * slots are consumed, subsequent parameterized effects' params are silently
 * dropped — the GPU budget is fixed at 2 floats.
 *
 * Non-parameterized effects (e.g. eDrawing) set their bit but consume no
 * param slots.
 *
 * Example: eDrawing + eWave(0.05, 4.0) → flags=0x06, p0=0.05, p1=4.0
 * Example: eGradient(0.5, 0.0) + eWave(0.05, 4.0) → flags=0x03,
 *          p0=0.5, p1=0.0 (gradient consumes both slots; wave params dropped)
 */
struct DEVICE_API GPUFaceData {
  uint32_t textureId;   ///< Index into TextureRecord[]
  uint32_t effectFlags; ///< Bitmask derived from effects array
  float params[2];

  /**
   * @brief Construct from a FaceMaterial
   *
   * Builds the effectFlags bitmask from the effects array and
   * packs parameters into the 2 available float slots in effect-slot order.
   */
  static GPUFaceData fromFaceMaterial(const FaceMaterial &fm) {
    uint32_t flags = 0;
    std::array<float, 2> slots = {0.0f, 0.0f};
    uint32_t slotsFilled = 0;

    for (const auto &fx : fm.effects) {
      if (!fx.isActive())
        continue;

      // Set the corresponding bit
      switch (fx.type) {
      case EffectType::eGradient:
        flags |= 0x01u;
        break;
      case EffectType::eWave:
        flags |= 0x02u;
        break;
      case EffectType::eDrawing:
        flags |= 0x04u;
        break;
      default:
        break;
      }

      // Pack params into the shared 2-slot budget
      uint32_t needed = faceEffectParamCount(fx.type);
      for (uint32_t i = 0; i < needed && slotsFilled < 2; ++i) {
        slots[slotsFilled++] = fx.params[i];
      }
    }

    return {fm.textureId, flags, {slots[0], slots[1]}};
  }
};
static_assert(sizeof(GPUFaceData) == 16,
              "GPUFaceData must be 16 bytes (std430)");

/**
 * @brief GPU-side per-vertex data (48 bytes, std430)
 *
 * Uploaded by Object to a storage buffer (set 0, binding 2) so that
 * each object can supply its own geometry to the shader.
 *
 * For 3D objects: (x, y, z, 1.0)
 * For 2D objects: (x, y, 0.0, 1.0)
 *
 * The color fields (r, g, b) carry per-vertex RGB color from
 * Vertex::color so the shader can use it instead of hardcoding.
 *
 * The normal fields (nx, ny, nz) store the pre-computed smooth vertex
 * normal (area-weighted average of adjacent face normals), computed by
 * the object_compute.slang compute shader at load time.  This replaces
 * the geometry shader's per-triangle normal calculation.
 */
struct DEVICE_API GPUVertexPosition {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
  float r = 1.0f;
  float g = 1.0f;
  float b = 1.0f;
  float pad0 = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  float nz = 0.0f;
  float npad = 0.0f;
};
static_assert(sizeof(GPUVertexPosition) == 48,
              "GPUVertexPosition must be 48 bytes (std430)");

} // namespace device

#endif // BINDLESS_TYPES_H_
