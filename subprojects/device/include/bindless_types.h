#ifndef BINDLESS_TYPES_H_
#define BINDLESS_TYPES_H_

#include "device_export.h"
#include <array>
#include <cstdint>
#include <limits>

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
  uint32_t textureId = TextureId::INVALID;
  uint32_t atlasTextureId = TextureId::INVALID;
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
 * @brief A single effect entry with type and parameters
 *
 * Designed for composability: faces store an array of FaceEffect
 * entries, each independently parameterised. The shader dispatches
 * each active effect and blends the results.
 */
struct DEVICE_API FaceEffect {
  EffectType type = EffectType::eNone;
  float param0 = 0.0f; ///< e.g. wave amplitude, gradient range
  float param1 = 0.0f; ///< e.g. wave frequency

  [[nodiscard]] bool isActive() const { return type != EffectType::eNone; }
};

/// Maximum number of simultaneous effects per face
inline constexpr uint32_t MAX_FACE_EFFECTS = 4;

/**
 * @brief Effect flag constants for per-face GPU data
 *
 * These bitmask values control per-face rendering effects in shaders.
 * A face can combine multiple effects via bitwise OR.
 * Kept for backward compatibility with shaders and GPU-side data.
 */
inline constexpr uint32_t EFFECT_NONE = 0;
inline constexpr uint32_t EFFECT_GRADIENT = 1u << 0;
inline constexpr uint32_t EFFECT_WAVE = 1u << 1;
inline constexpr uint32_t EFFECT_DRAWING = 1u << 2;

/**
 * @brief Unified per-face material description
 *
 * Used both on the CPU side (Object face material storage) and
 * for uploading to the GPU via an SSBO. The first 16 bytes
 * (textureId, effectFlags, effectParam0, effectParam1) match the
 * FaceData struct in shaders (std430 layout).
 *
 * The effects array provides a structured way to manage effects
 * on the CPU side. The effectFlags bitmask and effectParam0/1 fields
 * are the GPU-side representation, derived from the effects array
 * via buildEffectFlags().
 */
struct DEVICE_API FaceMaterial {
  // --- GPU-uploaded fields (first 16 bytes, std430 layout) ---
  uint32_t textureId = TextureId::INVALID; ///< TextureRecord index
  uint32_t effectFlags = 0;    ///< Bitmask: EFFECT_GRADIENT, EFFECT_WAVE, etc.
  float effectParam0 = 0.0f;  ///< Primary effect parameter (e.g. wave amplitude)
  float effectParam1 = 0.0f;  ///< Secondary effect parameter (e.g. wave frequency)

  // --- CPU-side structured effects ---
  std::array<FaceEffect, MAX_FACE_EFFECTS> effects{};

  /**
   * @brief Rebuild effectFlags and effectParam0/1 from the effects array
   *
   * Call this after modifying the effects array to keep the GPU-side
   * bitmask in sync. The first active effect's parameters are promoted
   * to effectParam0/effectParam1.
   */
  void buildEffectFlags() {
    effectFlags = EFFECT_NONE;
    effectParam0 = 0.0f;
    effectParam1 = 0.0f;
    bool firstParamsSet = false;

    for (const auto &fx : effects) {
      if (!fx.isActive())
        continue;

      switch (fx.type) {
      case EffectType::eGradient:
        effectFlags |= EFFECT_GRADIENT;
        break;
      case EffectType::eWave:
        effectFlags |= EFFECT_WAVE;
        break;
      case EffectType::eDrawing:
        effectFlags |= EFFECT_DRAWING;
        break;
      default:
        break;
      }

      if (!firstParamsSet) {
        effectParam0 = fx.param0;
        effectParam1 = fx.param1;
        firstParamsSet = true;
      }
    }
  }

  /**
   * @brief Add an effect to the first available slot
   * @return true if the effect was added, false if all slots are full
   */
  [[nodiscard]] bool addEffect(FaceEffect fx) {
    for (auto &slot : effects) {
      if (!slot.isActive()) {
        slot = fx;
        buildEffectFlags();
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
        buildEffectFlags();
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
    effectFlags = EFFECT_NONE;
    effectParam0 = 0.0f;
    effectParam1 = 0.0f;
  }

  /**
   * @brief Check if any effect is active
   */
  [[nodiscard]] bool hasEffects() const { return effectFlags != EFFECT_NONE; }
};

// The first 16 bytes of FaceMaterial match the GPU SSBO layout.
// Use offsetof to verify the GPU-relevant fields are at the expected offsets.
static_assert(offsetof(FaceMaterial, textureId) == 0,
              "FaceMaterial::textureId must be at offset 0");
static_assert(offsetof(FaceMaterial, effectFlags) == 4,
              "FaceMaterial::effectFlags must be at offset 4");
static_assert(offsetof(FaceMaterial, effectParam0) == 8,
              "FaceMaterial::effectParam0 must be at offset 8");
static_assert(offsetof(FaceMaterial, effectParam1) == 12,
              "FaceMaterial::effectParam1 must be at offset 12");

/**
 * @brief GPU-side per-face data (16 bytes, std430)
 *
 * This is the plain-old-data struct that is uploaded to the GPU SSBO.
 * Extracted from FaceMaterial for GPU upload.
 */
struct DEVICE_API GPUFaceData {
  uint32_t textureId;   ///< Index into TextureRecord[]
  uint32_t effectFlags; ///< Bitmask
  float effectParam0;
  float effectParam1;

  /**
   * @brief Construct from a FaceMaterial
   */
  static GPUFaceData fromFaceMaterial(const FaceMaterial &fm) {
    return {fm.textureId, fm.effectFlags, fm.effectParam0, fm.effectParam1};
  }
};
static_assert(sizeof(GPUFaceData) == 16,
              "GPUFaceData must be 16 bytes (std430)");

} // namespace device

#endif // BINDLESS_TYPES_H_
