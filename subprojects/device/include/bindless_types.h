#ifndef BINDLESS_TYPES_H_
#define BINDLESS_TYPES_H_

#include "device_export.h"
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

/**
 * @brief Effect flag constants for per-face GPU data
 *
 * These bitmask values control per-face rendering effects in shaders.
 * A face can combine multiple effects via bitwise OR.
 */
inline constexpr uint32_t EFFECT_NONE = 0;
inline constexpr uint32_t EFFECT_GRADIENT = 1u << 0;
inline constexpr uint32_t EFFECT_WAVE = 1u << 1;
inline constexpr uint32_t EFFECT_DRAWING = 1u << 2;

/**
 * @brief Per-face material description (CPU side)
 *
 * Describes which material/shader pipeline a face uses, which
 * TextureRecord it references, and per-face effect parameters.
 */
struct DEVICE_API FaceMaterialDesc {
  TextureId textureId;            ///< TextureRecord index
  uint32_t effectFlags = 0;       ///< Bitmask: EFFECT_GRADIENT, EFFECT_WAVE, etc.
  float effectParam0 = 0.0f;      ///< e.g. wave amplitude
  float effectParam1 = 0.0f;      ///< e.g. wave frequency
};

/**
 * @brief Per-face GPU data uploaded to an SSBO
 *
 * Must match the FaceData struct in shaders (std430 layout).
 */
struct DEVICE_API GPUFaceData {
  uint32_t textureId;   ///< Index into TextureRecord[]
  uint32_t effectFlags; ///< Bitmask
  float effectParam0;
  float effectParam1;
};
static_assert(sizeof(GPUFaceData) == 16,
              "GPUFaceData must be 16 bytes (std430)");

} // namespace device

#endif // BINDLESS_TYPES_H_
