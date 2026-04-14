// object_base_2d.slang
// Shader file for 2D objects with per-face material support.
//
// Uses indexed drawing (drawIndexed) with unique vertex positions.
// Wave displacement is computed inline in the vertex shader via an
// adjacency scan over the index buffer (binding 4), reading per-face
// effect data (binding 1) and base positions (binding 2).
// The fragment shader reads per-face data via SV_PrimitiveID.
//
// No geometry shader — 2D objects do not need face-normal lighting.
// Depth is hardcoded to 0.0 (front), w=1.0 for clip output.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 3×3 matrices, std140 padded)
//   binding 1: FaceData[] SSBO (per-face material data) — vertex + fragment
//   binding 2: VertexPosition[] SSBO (base positions, vertex + compute)
//   binding 4: Index[] SSBO (topology for adjacency scan, vertex + compute)
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
// Per-face data SSBO (set 0, binding 1) — read by vertex + fragment shaders
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Base vertex position SSBO (set 0, binding 2)
// Static undeformed positions. Wave displacement is applied inline.
// ---------------------------------------------------------------------------
struct VertexPosition {
    float x, y, z, w;
    float r, g, b, pad0;
    float nx, ny, nz, npad;
};
[[vk::binding(2, 0)]] StructuredBuffer<VertexPosition> basePositions;

// ---------------------------------------------------------------------------
// Index buffer as SSBO (set 0, binding 4)
// Used for adjacency scanning in the vertex shader.
// ---------------------------------------------------------------------------
[[vk::binding(4, 0)]] StructuredBuffer<uint> indexBuffer;

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
// Vertex shader — reads base positions and applies wave displacement inline
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Read 2D position from per-object base position SSBO
    VertexPosition vp = basePositions[vertexID];
    float2 pos = float2(vp.x, vp.y);

    // Inline 2D wave displacement: adjacency scan over all faces.
    float2 totalDisp = float2(0.0, 0.0);
    uint faceCount = pc.indexCount / 3u;
    for (uint fi = 0u; fi < faceCount; ++fi) {
        uint i0 = indexBuffer[fi * 3u + 0u];
        uint i1 = indexBuffer[fi * 3u + 1u];
        uint i2 = indexBuffer[fi * 3u + 2u];
        if (i0 != vertexID && i1 != vertexID && i2 != vertexID) continue;
        FaceData face = faceDataBuffer[fi];
        if ((face.effectFlags & EFFECT_WAVE) != 0u) {
            totalDisp += waveVertexDisplacement2D(pos, pc.time,
                                                  face.params[0], face.params[1]);
        }
    }
    pos += totalDisp;

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
