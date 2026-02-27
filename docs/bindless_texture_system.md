# Bindless Texture System – Design Document

## Overview

This document describes the GPU-driven, bindless texture/image/material
system for SapphireGem.  The primary goal is to eliminate fixed-size
texture arrays in shaders so that many objects can share the same pipeline
while referencing different textures via integer handles.

---

## 1  Handle Model

| Type | Storage | Description |
|------|---------|-------------|
| `TextureId` | `uint32_t index` | Index into the `TextureRecord[]` SSBO.  `UINT32_MAX` = invalid. |
| `ImageHandle` | `uint32_t index` | Index into one of the per-kind descriptor arrays (`images2D[]`, `atlases[]`, `maps[]`). |
| `ImageKind` | enum `{eImage2D, eAtlas, eMap}` | Discriminator selecting which descriptor array an `ImageHandle` belongs to. |

Handles are stable indices (not generational).  Once an image is
registered it keeps its index for the lifetime of the registry.  If
recycling is needed in the future a free-list + generation counter can
be added without changing the shader interface.

---

## 2  Descriptor Indexing Approach

### Required Vulkan features (Vulkan 1.2 core)

Requested via `core::VulkanConfig::enableDescriptorIndexing`:

* `descriptorIndexing`
* `shaderSampledImageArrayNonUniformIndexing`
* `runtimeDescriptorArray`
* `descriptorBindingPartiallyBound`
* `descriptorBindingVariableDescriptorCount`
* `descriptorBindingSampledImageUpdateAfterBind`

### Descriptor set layout

The bindless system uses **set 1** (per-device, global):

| Binding | Type | Count | Description |
|---------|------|-------|-------------|
| 0 | `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` | up to 4096 | `images2D[]` |
| 1 | `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` | up to 4096 | `atlases[]` |
| 2 | `VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE` | up to 4096 | `maps[]` |
| 3 | `VK_DESCRIPTOR_TYPE_SAMPLER` | 1 | Shared immutable sampler |
| 4 | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | 1 | `TextureRecord[]` SSBO |
| 5 | `VK_DESCRIPTOR_TYPE_STORAGE_BUFFER` | 1 | `TextureLayer[]` SSBO |

Image arrays use `PARTIALLY_BOUND` + `UPDATE_AFTER_BIND` flags.
Binding 2 (`maps[]`) additionally uses `VARIABLE_DESCRIPTOR_COUNT`.

**Set 0** remains per-object (UBO + push constants), unchanged from
the existing system.

### Fallback (no descriptor indexing)

If the physical device does not support descriptor indexing the
`ImageArrayRegistry` falls back to a fixed 16-element descriptor
array per kind (no variable count, no partial bind).  Shaders can
still index into the arrays but are limited to 16 images per kind.
This is clearly logged at initialisation.

---

## 3  Buffer Layouts

### TextureRecord (std430, 16 bytes)

```
offset  type      field
 0      uint32    firstLayer    // index into TextureLayer[]
 4      uint32    layerCount
 8      uint32    flags         // reserved
12      uint32    _pad0
```

### TextureLayer (std430, 80 bytes = 5 × vec4)

```
offset  type      field
 0      int32     image2DIndex      // -1 = none
 4      int32     atlasIndex        // -1 = none
 8      int32     mapIndex          // -1 = none
12      int32     processingType    // 0=none, 1=wave

16      float     atlasUvOffsetX
20      float     atlasUvOffsetY
24      float     atlasUvScaleX
28      float     atlasUvScaleY

32      float     tintR
36      float     tintG
40      float     tintB
44      float     tintA

48      float     rotation          // radians
52      float     scaleX
56      float     scaleY
60      uint32    blendMode         // 0=alpha, 1=add, 2=multiply

64      float     procAmplitude
68      float     procFrequency
72      float     procPhase
76      float     procSpeed
```

C++ `static_assert`s enforce sizes.  Shader struct definitions in
`bindless_common.slang` mirror these exactly.

---

## 4  Object / Submesh Overrides

```
Object
  ├── baseTextureId_  (TextureId)           ← default for all faces
  └── submeshTextureOverrides_              ← per-face overrides
        face 2 → TextureId X
        face 5 → TextureId Y
```

At draw time the effective `TextureId` for each face is:

1. If the face has an entry in `submeshTextureOverrides_`, use that.
2. Otherwise use `baseTextureId_`.

The `TextureId` is passed to the shader via push constants (alongside
the existing `time` float).  The shader uses it to index into
`TextureRecord[]` and then iterates the layer range.

This maps to the existing `render::Object::Submesh` pattern: the
current `materialIdentifier` / `textureIdentifier` fields become
`TextureId` values.

---

## 5  Multi-Thread Considerations

* **Image registration** (`ImageArrayRegistry::registerImage`) only
  touches a CPU-side vector protected by `registryMutex_`.  No Vulkan
  calls are made while the mutex is held.

* **Texture table building** (`TextureTableManager::addRecord`,
  `setLayers`) is similarly mutex-protected and CPU-only.

* **GPU upload / descriptor commit** must happen on the device
  thread (or with proper queue synchronisation).  These are separate
  entry points that the render loop calls after all registrations
  for the current batch are complete.

---

## 6  Multi-GPU Considerations

* Each GPU device creates its own `ImageArrayRegistry` and
  `TextureTableManager`.  The VMA allocator is already per-device
  (`VMAManager`).

* The same logical `TextureId` / `ImageHandle` can be used across
  devices because the CPU-side tables are duplicated per device.
  The handle is the *logical* index; each device's registry maps it
  to its own descriptor set.

* When multi-GPU is enabled, registration should be repeated for
  each device (or broadcast via `DeviceManager::forEachDevice`).

---

## 7  Compute Path (Future)

The `processingType` field in `TextureLayer` allows the fragment
shader to apply procedural effects (e.g. wave displacement).  When
compute pre-processing is added:

1. A compute pass writes the processed result into a new image.
2. The `image2DIndex` (or a new `processedImageIndex`) in the layer
   is updated to point to the pre-computed image.
3. `processingType` is set to `eNone` so the fragment shader just
   samples the pre-computed result.

This requires no structural changes to `TextureRecord` or
`TextureLayer`; only an additional field or reuse of an existing
index.

---

## 8  File Map

| File | Purpose |
|------|---------|
| `subprojects/device/include/bindless_types.h` | `TextureId`, `ImageHandle`, `ImageKind`, enums |
| `subprojects/device/include/image_array_registry.h` | `ImageArrayRegistry` – per-device descriptor arrays |
| `subprojects/device/include/texture_table.h` | `GPUTextureRecord`, `GPUTextureLayer`, `TextureTableManager` |
| `subprojects/device/src/image_array_registry.cpp` | Implementation |
| `subprojects/device/src/texture_table.cpp` | Implementation |
| `subprojects/core/include/config.h` | `VulkanConfig::enableDescriptorIndexing` flag |
| `subprojects/window/include/object.h` | `setTextureId`, `setSubmeshTextureOverride` |
| `assets/shaders/bindless_common.slang` | Shared GPU structs + `sampleBindlessTexture()` |
| `assets/shaders/cube_bindless.slang` | 3D cube using bindless sampling |
| `assets/shaders/quad2d_bindless.slang` | 2D quad using bindless sampling |
| `examples/bindless_texture_example.cpp` | Usage example |
