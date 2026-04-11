#ifndef BINDLESS_TYPES_H_
#define BINDLESS_TYPES_H_

#include "device_export.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
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
 * Used by Object::draw() to push time + objectId.
 */
struct DEVICE_API BindlessPushConstants {
  float time = 0.0f;
  uint32_t objectId = 0;
};

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
 * @brief A single effect entry with type and a per-effect parameter array.
 *
 * The parameter array length is determined by the effect type via
 * faceEffectParamCount(). Constructing with the wrong number of params
 * is a programmer error caught by assertion.
 */
struct DEVICE_API FaceEffect {
  EffectType type = EffectType::eNone;
  std::vector<float> params; ///< Length == faceEffectParamCount(type)

  FaceEffect() = default;

  /**
   * @brief Construct a FaceEffect with explicit params.
   *
   * @param t Effect type
   * @param p Parameter values — must have exactly faceEffectParamCount(t)
   * entries
   */
  explicit FaceEffect(EffectType t, std::vector<float> p = {})
      : type(t), params(std::move(p)) {
    assert(params.size() == faceEffectParamCount(type) &&
           "FaceEffect: wrong number of params for effect type");
  }

  /**
   * @brief Convenience constructor for effects with exactly 2 params.
   */
  FaceEffect(EffectType t, float p0, float p1) : type(t), params{p0, p1} {
    assert(faceEffectParamCount(t) == 2 &&
           "FaceEffect: effect type does not take 2 params");
  }

  [[nodiscard]] bool isActive() const { return type != EffectType::eNone; }

  /** @brief Safe parameter access — returns 0.0f for out-of-range indices. */
  [[nodiscard]] float param(uint32_t i) const {
    return i < params.size() ? params[i] : 0.0f;
  }
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
  void clearEffects() {
    effects = {};
  }

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
 */
struct DEVICE_API GPUFaceData {
  uint32_t textureId;   ///< Index into TextureRecord[]
  uint32_t effectFlags; ///< Bitmask derived from effects array
  float effectParam0;
  float effectParam1;

  /**
   * @brief Construct from a FaceMaterial
   *
   * Builds the effectFlags bitmask from the effects array and
   * promotes the first active effect's parameters to effectParam0/1.
   */
  static GPUFaceData fromFaceMaterial(const FaceMaterial &fm) {
    uint32_t flags = 0;
    float p0 = 0.0f;
    float p1 = 0.0f;
    bool firstParamsSet = false;

    for (const auto &fx : fm.effects) {
      if (!fx.isActive())
        continue;

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

      if (!firstParamsSet) {
        p0 = fx.param(0);
        p1 = fx.param(1);
        firstParamsSet = true;
      }
    }

    return {fm.textureId, flags, p0, p1};
  }
};
static_assert(sizeof(GPUFaceData) == 16,
              "GPUFaceData must be 16 bytes (std430)");

} // namespace device

#endif // BINDLESS_TYPES_H_
