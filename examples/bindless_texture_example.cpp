// bindless_texture_example.cpp
// ============================================================================
// Example usage of the bindless texture system.
//
// This file demonstrates:
//   1. Creating a layered texture (3 layers with different tints/rotations)
//   2. Overriding a submesh/section texture
//   3. Using an atlas layer with UV rect + wave processing
//
// NOTE: This is a standalone example that shows API usage patterns.
//       It is NOT compiled as part of the main engine build.
// ============================================================================

#include "bindless_types.h"
#include "image_array_registry.h"
#include "object.h"
#include "texture.h"
#include "texture_table.h"
#include "vma_allocator.h"
#include "vulkan_device.h"

// Assume these are available from the engine initialisation:
//   device::GPUDevice&       primaryDevice;
//   device::VMAAllocator&    allocator;
//   device::ShaderManager&   shaderManager;

namespace example {

// ============================================================================
// Static tags (must have static storage duration)
// ============================================================================

// Atlas containing multiple sub-images (512×512, 3×2 grid)
static const window::AtlasTag EXAMPLE_ATLAS{
    "example_atlas", "assets/textures/layer_atlas.png", 512, 512};

// Standalone images
static const window::ImageTag BASE_COLOR_IMAGE{
    window::ImageFromFile{"base_color", "assets/textures/checkerboard.png"}};

static const window::ImageTag OVERLAY_IMAGE{
    window::ImageFromFile{"overlay", "assets/textures/gradient.png"}};

// Atlas region (heart shape, second row first column)
static const window::ImageTag ATLAS_HEART{window::ImageFromAtlasRegion{
    "heart", &EXAMPLE_ATLAS, 0, 256, 170, 256}};

// ============================================================================
// Example 1: Create a layered texture with 3 layers
// ============================================================================

void createLayeredTexture(device::ImageArrayRegistry &registry,
                          device::TextureTableManager &table,
                          device::VMAAllocator &allocator,
                          device::GPUDevice &device) {
  // --- Step 1: Upload images to GPU and register in the image arrays ---

  // Upload base colour image (assume Texture class handles GPU upload)
  window::Texture baseTex(window::TextureTag{"base", nullptr, 0});
  // ... upload baseTex ...

  // Register images in the bindless registry.
  // In a real scenario, the image would be uploaded first, then the
  // VkImageView obtained from the AllocatedImage.
  //
  // For illustration we show the API pattern:
  //
  //   device::ImageHandle baseHandle =
  //       registry.registerImage(device::ImageKind::eImage2D,
  //                              baseColorAllocatedImage.getView());
  //
  //   device::ImageHandle overlayHandle =
  //       registry.registerImage(device::ImageKind::eImage2D,
  //                              overlayAllocatedImage.getView());
  //
  //   device::ImageHandle atlasHandle =
  //       registry.registerImage(device::ImageKind::eAtlas,
  //                              atlasAllocatedImage.getView());

  // Simulated handles for this example:
  device::ImageHandle baseHandle{0};    // images2D[0]
  device::ImageHandle overlayHandle{1}; // images2D[1]
  device::ImageHandle atlasHandle{0};   // atlases[0]

  // --- Step 2: Build a 3-layer TextureRecord ---

  device::TextureId texId = table.addRecord(3);

  std::vector<device::GPUTextureLayer> layers(3);

  // Layer 0: base colour image, no transform
  layers[0] = {};
  layers[0].image2DIndex = static_cast<int32_t>(baseHandle.index);
  layers[0].atlasIndex = -1;
  layers[0].mapIndex = -1;
  layers[0].processingType = 0; // none
  layers[0].atlasUvOffsetX = 0.0f;
  layers[0].atlasUvOffsetY = 0.0f;
  layers[0].atlasUvScaleX = 1.0f;
  layers[0].atlasUvScaleY = 1.0f;
  layers[0].tintR = 1.0f;
  layers[0].tintG = 0.8f; // warm tint
  layers[0].tintB = 0.6f;
  layers[0].tintA = 1.0f;
  layers[0].rotation = 0.0f;
  layers[0].scaleX = 1.0f;
  layers[0].scaleY = 1.0f;
  layers[0].blendMode = 0; // alpha
  layers[0].procAmplitude = 0.0f;
  layers[0].procFrequency = 0.0f;
  layers[0].procPhase = 0.0f;
  layers[0].procSpeed = 0.0f;

  // Layer 1: overlay image with rotation and blue tint
  layers[1] = {};
  layers[1].image2DIndex = static_cast<int32_t>(overlayHandle.index);
  layers[1].atlasIndex = -1;
  layers[1].mapIndex = -1;
  layers[1].processingType = 0;
  layers[1].atlasUvOffsetX = 0.0f;
  layers[1].atlasUvOffsetY = 0.0f;
  layers[1].atlasUvScaleX = 1.0f;
  layers[1].atlasUvScaleY = 1.0f;
  layers[1].tintR = 0.5f;
  layers[1].tintG = 0.7f;
  layers[1].tintB = 1.0f; // blue tint
  layers[1].tintA = 0.6f; // semi-transparent
  layers[1].rotation = 0.785f; // 45 degrees
  layers[1].scaleX = 0.8f;
  layers[1].scaleY = 0.8f;
  layers[1].blendMode = 0; // alpha
  layers[1].procAmplitude = 0.0f;
  layers[1].procFrequency = 0.0f;
  layers[1].procPhase = 0.0f;
  layers[1].procSpeed = 0.0f;

  // Layer 2: atlas heart with wave processing
  layers[2] = {};
  layers[2].image2DIndex = -1; // not a standalone image
  layers[2].atlasIndex = static_cast<int32_t>(atlasHandle.index);
  layers[2].mapIndex = -1;
  layers[2].processingType = 1; // wave
  // Atlas UV rect for the heart region (row 1, col 0 of 3×2 grid)
  layers[2].atlasUvOffsetX = 0.0f;
  layers[2].atlasUvOffsetY = 0.5f; // second row
  layers[2].atlasUvScaleX = 170.0f / 512.0f; // cell width / atlas width
  layers[2].atlasUvScaleY = 256.0f / 512.0f; // cell height / atlas height
  layers[2].tintR = 1.0f;
  layers[2].tintG = 0.3f;
  layers[2].tintB = 0.3f; // reddish tint for heart
  layers[2].tintA = 0.8f;
  layers[2].rotation = 0.0f;
  layers[2].scaleX = 1.0f;
  layers[2].scaleY = 1.0f;
  layers[2].blendMode = 0;
  // Wave processing params
  layers[2].procAmplitude = 0.02f;
  layers[2].procFrequency = 8.0f;
  layers[2].procPhase = 0.0f;
  layers[2].procSpeed = 2.0f;

  table.setLayers(texId, layers);

  // --- Step 3: Upload tables to GPU ---
  table.uploadToGPU(allocator, device);

  // --- Step 4: Commit descriptors ---
  // registry.commitDescriptors(device, sampler,
  //                            &table.getRecordBuffer(),
  //                            &table.getLayerBuffer());

  // texId is now usable: pass it to an Object via setTextureId(texId)
  (void)texId;
}

// ============================================================================
// Example 2: Override a submesh texture
// ============================================================================

void overrideSubmeshTexture(device::TextureTableManager &table) {
  // Create a second texture record for the override
  device::TextureId overrideTexId = table.addRecord(1);

  std::vector<device::GPUTextureLayer> overrideLayers(1);
  overrideLayers[0] = {};
  overrideLayers[0].image2DIndex = 1; // images2D[1] – different image
  overrideLayers[0].atlasIndex = -1;
  overrideLayers[0].mapIndex = -1;
  overrideLayers[0].processingType = 0;
  overrideLayers[0].atlasUvOffsetX = 0.0f;
  overrideLayers[0].atlasUvOffsetY = 0.0f;
  overrideLayers[0].atlasUvScaleX = 1.0f;
  overrideLayers[0].atlasUvScaleY = 1.0f;
  overrideLayers[0].tintR = 0.2f;
  overrideLayers[0].tintG = 1.0f;
  overrideLayers[0].tintB = 0.2f; // green tint
  overrideLayers[0].tintA = 1.0f;
  overrideLayers[0].rotation = 0.0f;
  overrideLayers[0].scaleX = 1.0f;
  overrideLayers[0].scaleY = 1.0f;
  overrideLayers[0].blendMode = 0;
  overrideLayers[0].procAmplitude = 0.0f;
  overrideLayers[0].procFrequency = 0.0f;
  overrideLayers[0].procPhase = 0.0f;
  overrideLayers[0].procSpeed = 0.0f;

  table.setLayers(overrideTexId, overrideLayers);

  // Apply the override to face 2 of an object:
  //
  //   myObject.setTextureId(baseTexId);            // base texture
  //   myObject.setSubmeshTextureOverride(2, overrideTexId);  // face 2 override
  //
  // At draw time, the shader for face 2 will use overrideTexId
  // instead of baseTexId.

  (void)overrideTexId;
}

// ============================================================================
// Example 3: Atlas layer with UV rect + wave processing
// ============================================================================

void atlasWithWaveProcessing(device::TextureTableManager &table) {
  // Create a single-layer texture that samples from an atlas
  // with wave UV displacement.
  device::TextureId waveTexId = table.addRecord(1);

  std::vector<device::GPUTextureLayer> waveLayers(1);
  waveLayers[0] = {};
  waveLayers[0].image2DIndex = -1;
  waveLayers[0].atlasIndex = 0; // atlases[0]
  waveLayers[0].mapIndex = -1;
  waveLayers[0].processingType = 1; // wave

  // UV rect: star shape at column 1, row 0 of 3×2 atlas
  waveLayers[0].atlasUvOffsetX = 170.0f / 512.0f; // col 1
  waveLayers[0].atlasUvOffsetY = 0.0f;             // row 0
  waveLayers[0].atlasUvScaleX = 170.0f / 512.0f;
  waveLayers[0].atlasUvScaleY = 256.0f / 512.0f;

  waveLayers[0].tintR = 1.0f;
  waveLayers[0].tintG = 1.0f;
  waveLayers[0].tintB = 1.0f;
  waveLayers[0].tintA = 1.0f;
  waveLayers[0].rotation = 0.0f;
  waveLayers[0].scaleX = 1.0f;
  waveLayers[0].scaleY = 1.0f;
  waveLayers[0].blendMode = 0;

  // Wave parameters: gentle ripple
  waveLayers[0].procAmplitude = 0.03f;
  waveLayers[0].procFrequency = 6.0f;
  waveLayers[0].procPhase = 0.0f;
  waveLayers[0].procSpeed = 1.5f;

  table.setLayers(waveTexId, waveLayers);

  // This textureId can now be assigned to any object.
  // The fragment shader will:
  //   1. Read TextureRecord[waveTexId.index] → firstLayer, layerCount=1
  //   2. Read TextureLayer[firstLayer] → atlasIndex=0, processingType=1
  //   3. Sample atlases[0] with atlas UV transform
  //   4. Apply wave displacement to UVs before sampling
  //   5. Apply tint and output final colour

  (void)waveTexId;
}

} // namespace example
