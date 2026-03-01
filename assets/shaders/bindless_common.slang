// bindless_common.slang
// Shared struct definitions and utilities for the bindless texture system.
//
// These structs MUST match the C++ definitions in texture_table.h exactly
// (std430 layout, same sizes and offsets).
//
// Usage: #include this file from any shader that needs to access
// the texture table or sample images via bindless handles.

// ---------------------------------------------------------------------------
// GPU texture record – indexes into TextureLayer[]
// ---------------------------------------------------------------------------
struct TextureRecord {
    uint firstLayer;   // Index of first TextureLayer
    uint layerCount;   // Number of layers to composite
    uint flags;        // Reserved
    uint _pad0;
};

// ---------------------------------------------------------------------------
// GPU texture layer – describes one compositing layer
// ---------------------------------------------------------------------------
struct TextureLayer {
    // Image references (-1 = none)
    int image2DIndex;
    int atlasIndex;
    int mapIndex;
    int processingType;  // 0=none, 1=wave

    // Atlas UV rect transform (offset + scale)
    float atlasUvOffsetX;
    float atlasUvOffsetY;
    float atlasUvScaleX;
    float atlasUvScaleY;

    // Tint colour (RGBA)
    float tintR;
    float tintG;
    float tintB;
    float tintA;

    // Transform + blend mode
    float rotation;   // radians
    float scaleX;
    float scaleY;
    uint blendMode;   // 0=alpha, 1=additive, 2=multiply

    // Processing parameters (wave etc.)
    float procAmplitude;
    float procFrequency;
    float procPhase;
    float procSpeed;
};

// ---------------------------------------------------------------------------
// Bindless descriptor set  (set 1)
// ---------------------------------------------------------------------------
// Image arrays are placed at the highest binding numbers so that
// VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT (required to be
// on the highest binding) lands on the last image array (maps, binding 5).
//
// Binding 0: shared sampler
// Binding 1: TextureRecord SSBO
// Binding 2: TextureLayer SSBO
// Binding 3: sampled images – images2D[]
// Binding 4: sampled images – atlases[]
// Binding 5: sampled images – maps[]

[[vk::binding(0, 1)]] SamplerState     bindless_sampler;
[[vk::binding(1, 1)]] StructuredBuffer<TextureRecord> textureRecords;
[[vk::binding(2, 1)]] StructuredBuffer<TextureLayer>  textureLayers;
[[vk::binding(3, 1)]] Texture2D        bindless_images2D[];
[[vk::binding(4, 1)]] Texture2D        bindless_atlases[];
[[vk::binding(5, 1)]] Texture2D        bindless_maps[];

// ---------------------------------------------------------------------------
// Maximum layer loop count – bounded for performance
// ---------------------------------------------------------------------------
static const uint MAX_LAYER_LOOP = 8;

// Minimum scale to avoid division by zero
static const float MIN_SCALE = 0.001;

// ---------------------------------------------------------------------------
// Helper: apply 2D rotation to UV around (0.5, 0.5)
// ---------------------------------------------------------------------------
float2 rotateUV(float2 uv, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    float2 centred = uv - float2(0.5, 0.5);
    float2 rotated = float2(centred.x * c - centred.y * s,
                            centred.x * s + centred.y * c);
    return rotated + float2(0.5, 0.5);
}

// ---------------------------------------------------------------------------
// Helper: apply atlas UV rect transform
// ---------------------------------------------------------------------------
float2 atlasTransformUV(float2 uv, float4 atlasRect) {
    return uv * atlasRect.zw + atlasRect.xy;
}

// ---------------------------------------------------------------------------
// Helper: wave-based UV displacement
// ---------------------------------------------------------------------------
float2 waveDisplaceUV(float2 uv, float amplitude, float frequency,
                      float phase, float speed, float time) {
    float dx = sin(uv.y * frequency + time * speed + phase) * amplitude;
    float dy = cos(uv.x * frequency + time * speed + phase) * amplitude;
    return uv + float2(dx, dy);
}

// ---------------------------------------------------------------------------
// Helper: blend two colours according to blend mode
// ---------------------------------------------------------------------------
float4 blendLayer(float4 base, float4 layer, uint mode) {
    switch (mode) {
        case 1:  // additive
            return float4(base.rgb + layer.rgb * layer.a, base.a);
        case 2:  // multiply
            return float4(lerp(base.rgb, base.rgb * layer.rgb, layer.a), base.a);
        default: // 0 = alpha blend
            return lerp(base, layer, layer.a);
    }
}

// ---------------------------------------------------------------------------
// Main sampling function: sample a full texture via its TextureId
// ---------------------------------------------------------------------------
float4 sampleBindlessTexture(uint textureId, float2 uv, float time) {
    TextureRecord rec = textureRecords[textureId];

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    bool firstLayer = true;

    uint end = min(rec.layerCount, MAX_LAYER_LOOP);
    for (uint i = 0; i < end; i++) {
        TextureLayer layer = textureLayers[rec.firstLayer + i];

        // Start with base UV
        float2 layerUV = uv;

        // Apply layer scale
        layerUV = (layerUV - float2(0.5, 0.5)) *
                  float2(1.0 / max(layer.scaleX, MIN_SCALE),
                         1.0 / max(layer.scaleY, MIN_SCALE)) +
                  float2(0.5, 0.5);

        // Apply layer rotation
        if (layer.rotation != 0.0) {
            layerUV = rotateUV(layerUV, layer.rotation);
        }

        // Apply atlas UV transform (if image comes from an atlas)
        if (layer.atlasIndex >= 0) {
            float4 atlasRect = float4(layer.atlasUvOffsetX,
                                      layer.atlasUvOffsetY,
                                      layer.atlasUvScaleX,
                                      layer.atlasUvScaleY);
            layerUV = atlasTransformUV(layerUV, atlasRect);
        }

        // Apply wave processing
        if (layer.processingType == 1) {
            layerUV = waveDisplaceUV(layerUV, layer.procAmplitude,
                                     layer.procFrequency, layer.procPhase,
                                     layer.procSpeed, time);
        }

        // Sample the image via bindless descriptor index
        float4 colour = float4(1.0, 0.0, 1.0, 1.0); // magenta fallback
        if (layer.image2DIndex >= 0) {
            colour = bindless_images2D[NonUniformResourceIndex(layer.image2DIndex)]
                         .Sample(bindless_sampler, layerUV);
        } else if (layer.atlasIndex >= 0) {
            colour = bindless_atlases[NonUniformResourceIndex(layer.atlasIndex)]
                         .Sample(bindless_sampler, layerUV);
        } else if (layer.mapIndex >= 0) {
            colour = bindless_maps[NonUniformResourceIndex(layer.mapIndex)]
                         .Sample(bindless_sampler, layerUV);
        }

        // Apply tint
        colour *= float4(layer.tintR, layer.tintG, layer.tintB, layer.tintA);

        // Blend into result
        if (firstLayer) {
            result = colour;
            firstLayer = false;
        } else {
            result = blendLayer(result, colour, layer.blendMode);
        }
    }

    return result;
}
