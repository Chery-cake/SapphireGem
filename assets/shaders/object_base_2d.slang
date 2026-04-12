// object_base_2d.slang
// Shader file for 2D objects with per-face material support.
//
// Uses indexed drawing (drawIndexed) with unique vertex positions.
// The compute shader (object_update.slang) runs per-frame to displace
// vertices based on per-face effects (wave, etc.) via adjacency scanning.
// The vertex shader reads pre-displaced positions from binding 3.
// The fragment shader reads per-face data via SV_PrimitiveID.
//
// No geometry shader — 2D objects do not need face-normal lighting.
// Depth is hardcoded to 0.0 (front), w=1.0 for clip output.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 3×3 matrices, std140 padded)
//   binding 1: FaceData[] SSBO (per-face material data) — fragment only
//   binding 3: Displaced VertexPosition[] SSBO (per-frame, compute-written)
// Descriptor set 1: global bindless set (from bindless_common.slang)

import bindless_common;
import face_effects;

// ---------------------------------------------------------------------------
// UBO: 3×3 matrices for 2D objects (std140 padded: each column = vec4)
// ---------------------------------------------------------------------------
struct Std140Mat3 {
    float4 col0;   // x, y, z, pad
    float4 col1;   // x, y, z, pad
    float4 col2;   // x, y, z, pad
};

struct UniformBuffer2D {
    Std140Mat3 model;
    Std140Mat3 view;
    Std140Mat3 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer2D> ubo;

// ---------------------------------------------------------------------------
// Per-face data SSBO (set 0, binding 1) — read by fragment shader
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Displaced vertex position SSBO (set 0, binding 3)
// Written by compute shader each frame.
// ---------------------------------------------------------------------------
struct VertexPosition {
    float x, y, z, w;
    float r, g, b, pad0;
    float nx, ny, nz, npad;
};
[[vk::binding(3, 0)]] StructuredBuffer<VertexPosition> displacedPositions;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
struct PushData {
    float time;
    uint  objectId;
    uint  vertexCount;
    uint  indexCount;
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

// ---------------------------------------------------------------------------
// Shader I/O structs
// ---------------------------------------------------------------------------
struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 modelPos;
};

// ---------------------------------------------------------------------------
// Helper: unpack Std140Mat3 to a usable 3×3 matrix multiply
// ---------------------------------------------------------------------------
float3 mulMat3(Std140Mat3 m, float3 v) {
    return float3(
        m.col0.x * v.x + m.col1.x * v.y + m.col2.x * v.z,
        m.col0.y * v.x + m.col1.y * v.y + m.col2.y * v.z,
        m.col0.z * v.x + m.col1.z * v.y + m.col2.z * v.z
    );
}

// ---------------------------------------------------------------------------
// Vertex shader — reads pre-displaced positions, no per-face data access
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Read 2D position from displaced per-object SSBO
    VertexPosition vp = displacedPositions[vertexID];
    float2 pos = float2(vp.x, vp.y);
    output.modelPos = pos;

    // Transform through model → view → proj (all 3×3)
    float3 modelPos3 = mulMat3(ubo.model, float3(pos, 1.0));
    float3 viewPos3  = mulMat3(ubo.view,  modelPos3);
    float3 clipPos3  = mulMat3(ubo.proj,  viewPos3);

    // 2D: depth=0 (front), w=1
    output.pos = float4(clipPos3.xy, 0.0, 1.0);

    output.color = float3(vp.r, vp.g, vp.b);

    return output;
}

// ---------------------------------------------------------------------------
// Fragment shader — reads per-face data via SV_PrimitiveID
// ---------------------------------------------------------------------------
[shader("fragment")]
float4 fragMain(VSOutput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET {
    FaceData face = faceDataBuffer[primitiveID];

    uint flags = face.effectFlags;
    uint texId = face.textureId;

    // Generate UV from model position for 2D
    float2 uv = input.modelPos * 0.5 + float2(0.5, 0.5);

    // If face has effects, dispatch them
    if (flags != 0) {
        float4 effectColor = dispatchFaceEffect(
            flags, uv, input.modelPos, pc.time,
            face.params[0], face.params[1], 1.0);

        // If there's also a texture, blend it
        if (texId != 0xFFFFFFFF) {
            float4 texColor = sampleBindlessTexture(texId, uv, pc.time);
            return lerp(texColor, effectColor, 0.5);
        }
        return effectColor;
    }

    // No effects — check for texture
    if (texId != 0xFFFFFFFF) {
        return sampleBindlessTexture(texId, uv, pc.time);
    }

    // Plain colour fallback
    return float4(input.color, 1.0);
}
