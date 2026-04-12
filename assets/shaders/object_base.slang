// object_base.slang
// Primary shader file for 3D objects with per-face material support.
//
// Uses indexed drawing (drawIndexed) with unique vertex positions.
// The compute shader (object_update.slang) runs per-frame to displace
// vertices based on per-face effects (wave, etc.) via adjacency scanning.
// The vertex shader reads pre-displaced positions from binding 3.
// The fragment shader reads per-face data via SV_PrimitiveID.
//
// NO geometry shader — normals are pre-computed by the compute shader
// (object_compute.slang) and stored in the vertex position buffer.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 4×4 matrices)
//   binding 1: FaceData[] SSBO (per-face material data) — fragment only
//   binding 3: Displaced VertexPosition[] SSBO (per-frame, compute-written)
// Descriptor set 1: global bindless set (from bindless_common.slang)

import bindless_common;
import face_effects;

// ---------------------------------------------------------------------------
// UBO: 4x4 matrices for 3D objects
// ---------------------------------------------------------------------------
struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

// ---------------------------------------------------------------------------
// Per-face data SSBO (set 0, binding 1) — read by fragment shader
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Displaced vertex position SSBO (set 0, binding 3)
// Written by compute shader each frame. Contains displaced positions + normals.
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
    float4 pos       : SV_Position;
    float3 worldPos;
    float3 normal;
    float3 color;
    float3 modelPos;    // Object-space position for UV generation
    float3 modelNormal; // Object-space normal for UV axis selection
};

// ---------------------------------------------------------------------------
// Box-map UV helper: projects world position onto the face with the
// largest component of the normal.
// Note: UVs use raw world-space coordinates which works well for
// unit-scale objects (cube ±0.5, polytope ~±0.3). For larger objects,
// the shader caller may want to fract() or scale these coordinates.
// ---------------------------------------------------------------------------
float2 boxMapUV(float3 worldPos, float3 normal) {
    float3 absN = abs(normal);
    if (absN.x >= absN.y && absN.x >= absN.z) return worldPos.yz;
    if (absN.y >= absN.x && absN.y >= absN.z) return worldPos.xz;
    return worldPos.xy;
}

// ---------------------------------------------------------------------------
// Vertex shader — reads pre-displaced positions, no per-face data access
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    VertexPosition vp = displacedPositions[vertexID];
    float3 pos    = float3(vp.x, vp.y, vp.z);
    float3 normal = float3(vp.nx, vp.ny, vp.nz);

    float4 worldPos4 = mul(ubo.model, float4(pos, 1.0));
    output.worldPos    = worldPos4.xyz;
    output.normal      = normalize(mul((float3x3)ubo.model, normal));
    output.pos         = mul(ubo.proj, mul(ubo.view, worldPos4));
    output.color       = float3(vp.r, vp.g, vp.b);
    output.modelPos    = pos;    // Object-space position (fixed to the face)
    output.modelNormal = normal; // Object-space normal (fixed to the face)

    return output;
}

// ---------------------------------------------------------------------------
// Fragment shader — reads per-face data via SV_PrimitiveID
// ---------------------------------------------------------------------------
[shader("fragment")]
float4 fragMain(VSOutput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET {
    FaceData face = faceDataBuffer[primitiveID];

    // Lighting from pre-computed normal
    float3 lightDir    = normalize(float3(0.3, 1.0, 0.5));
    float  lightFactor = max(dot(normalize(input.normal), lightDir), 0.3);

    // UV: box-mapped from object-space position and object-space normal.
    // Using model-space coordinates ensures the texture stays fixed to the
    // face regardless of the object's world-space position or rotation.
    float2 uv = boxMapUV(input.modelPos, input.modelNormal);

    uint flags = face.effectFlags;
    uint texId = face.textureId;

    // Base colour: textured or vertex colour with lighting
    float4 baseColor;
    if (texId != 0xFFFFFFFF) {
        baseColor = sampleBindlessTexture(texId, uv, pc.time);
    } else {
        baseColor = float4(input.color * lightFactor, 1.0);
    }

    // Apply per-face effects if any
    if (flags != 0u) {
        float4 fx = dispatchFaceEffect(flags, uv, input.worldPos.xy,
                                       pc.time, face.params[0], face.params[1], 1.0);
        if (texId != 0xFFFFFFFF)
            return lerp(baseColor, fx, 0.5);
        return fx * float4(float3(lightFactor, lightFactor, lightFactor), 1.0);
    }

    return baseColor;
}
