#ifndef BINDLESS_TYPES_H_
#define BINDLESS_TYPES_H_

#include "device_export.h"
#include <array>
#include <cstdint>
#include <limits>
#include <print>

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

/// Maximum total floats (params + padding) per FaceEffect, so that
/// sizeof(FaceEffect) is always a known compile-time multiple of 16 bytes.
inline constexpr uint32_t FACE_EFFECT_TOTAL_FLOATS = 4; // 16 bytes of float data

/**
 * @brief A single effect entry with type, param count, and a fixed-size data
 *        array.
 *
 * The data array always has FACE_EFFECT_TOTAL_FLOATS slots, zero-initialized.
 * The first `paramCount` slots hold meaningful parameters; the rest are padding.
 * sizeof(FaceEffect) == 24 bytes (4 + 4 + 16).
 */
struct DEVICE_API FaceEffect {
  EffectType type = EffectType::eNone;
  uint32_t paramCount = 0; ///< How many data[] entries are meaningful
  std::array<float, FACE_EFFECT_TOTAL_FLOATS> data{}; ///< Zero-initialized

  FaceEffect() = default;

  /**
   * @brief Construct a FaceEffect with just a type (zero-param effects).
   */
  explicit FaceEffect(EffectType t) : type(t), paramCount(0), data{} {}

  /**
   * @brief Construct from an initializer list of params.
   *
   * Fills data[0..n-1] from params, zero-fills the rest.
   * If more params than FACE_EFFECT_TOTAL_FLOATS are given, truncates with
   * a warning.
   */
  FaceEffect(EffectType t, std::initializer_list<float> params)
      : type(t),
        paramCount(static_cast<uint32_t>(
            std::min(params.size(),
                     static_cast<size_t>(FACE_EFFECT_TOTAL_FLOATS)))),
        data{} {
    if (params.size() > FACE_EFFECT_TOTAL_FLOATS) {
      std::println(stderr,
                   "[FaceEffect] Warning: effect type {} was given {} params "
                   "but max is {} — truncating",
                   static_cast<uint32_t>(t), params.size(),
                   FACE_EFFECT_TOTAL_FLOATS);
    }
    uint32_t i = 0;
    for (float p : params) {
      if (i >= FACE_EFFECT_TOTAL_FLOATS)
        break;
      data[i++] = p;
    }
  }

  /**
   * @brief Convenience constructor for effects with exactly 2 params.
   */
  FaceEffect(EffectType t, float p0, float p1) : FaceEffect(t, {p0, p1}) {}

  [[nodiscard]] bool isActive() const { return type != EffectType::eNone; }
};
static_assert(sizeof(FaceEffect) == 24, "FaceEffect must be 24 bytes");

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
 * There are only 2 float param slots for all effects combined on a face.
 * Params are packed in effect-slot order: the first parameterized effect
 * fills as many slots as it needs (up to 2), then the second parameterized
 * effect takes whatever remains. If both slots are consumed, subsequent
 * parameterized effects' params are silently dropped — the GPU budget is
 * fixed at 2 floats.
 *
 * Non-parameterized effects (e.g. eDrawing) set their bit but consume no
 * param slots.
 *
 * params[0]: first param of the first parameterized active effect
 * params[1]: second param of the first parameterized active effect
 *            (or first param of the second if the first has only 1 param)
 *
 * Example: eDrawing + eWave(0.05, 4.0) → flags=0x06, params={0.05, 4.0}
 * Example: eGradient(0.5, 0.0) + eWave(0.05, 4.0) → flags=0x03,
 *          params={0.5, 0.0} (gradient consumes both; wave params dropped)
 */
struct DEVICE_API GPUFaceData {
  uint32_t textureId;   ///< Index into TextureRecord[]
  uint32_t effectFlags; ///< Bitmask derived from effects array
  float params[2];      ///< Packed params, same layout on GPU

  /**
   * @brief Construct from a FaceMaterial
   *
   * Builds the effectFlags bitmask from the effects array and
   * packs parameters into the 2 available float slots in effect-slot order.
   */
  static GPUFaceData fromFaceMaterial(const FaceMaterial &fm) {
    uint32_t flags = 0;
    float slots[2] = {0.0f, 0.0f};
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
        slots[slotsFilled++] = fx.data[i];
      }
    }

    return {fm.textureId, flags, {slots[0], slots[1]}};
  }
};
static_assert(sizeof(GPUFaceData) == 16,
              "GPUFaceData must be 16 bytes (std430)");

} // namespace device

#endif // BINDLESS_TYPES_H_
