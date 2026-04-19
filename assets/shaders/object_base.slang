// object_base.slang
// Primary shader file for 3D objects with per-face material support.
//
// Uses indexed drawing (drawIndexed) with unique vertex positions.
// The compute shader (object_update.slang) runs per-frame to displace
// vertices based on per-face effects (wave, etc.) via adjacency scanning.
// The vertex shader reads pre-displaced positions from binding 3 for
// rendering, and base (undisplaced) positions from binding 2 for UV
// generation — this keeps texture mapping stable even when adjacent
// faces apply wave displacement to shared vertices.
// The fragment shader reads per-face data via SV_PrimitiveID.
//
// NO geometry shader — normals are pre-computed by the compute shader
// (object_compute.slang) and stored in the vertex position buffer.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 4×4 matrices)
//   binding 1: FaceData[] SSBO (per-face material data) — fragment only
//   binding 2: Base VertexPosition[] SSBO (static, undeformed) — vertex reads for UV
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
// Base vertex position SSBO (set 0, binding 2)
// Static, undeformed positions. Used by the vertex shader for UV generation
// so that textures stay fixed to faces even when wave displacement moves
// shared vertices.
// ---------------------------------------------------------------------------
struct VertexPosition {
    float x, y, z, w;
    float r, g, b, pad0;
    float nx, ny, nz, npad;
};
[[vk::binding(2, 0)]] StructuredBuffer<VertexPosition> basePositions;

// ---------------------------------------------------------------------------
// Displaced vertex position SSBO (set 0, binding 3)
// Written by compute shader each frame. Contains displaced positions + normals.
// Used for the actual rendering position (clip space).
// ---------------------------------------------------------------------------
[[vk::binding(3, 0)]] StructuredBuffer<VertexPosition> displacedPositions;

// ---------------------------------------------------------------------------
// Index buffer SSBO (set 0, binding 4) – stores uint32 indices
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
    float4 pos       : SV_Position;
    float3 worldPos;
    float3 normal;
    float3 color;
    float3 modelPos;    // Base (undisplaced) object-space position for UV
};

// ---------------------------------------------------------------------------
// Box-map UV helper: projects position onto the plane perpendicular to
// the dominant component of the normal.
// Returns coordinates in model space; the caller adds +0.5 to remap
// from [-0.5, 0.5] (unit cube) to texture-space [0, 1].
// ---------------------------------------------------------------------------
float2 boxMapUV(float3 pos, float3 normal) {
    float3 absN = abs(normal);
    if (absN.x >= absN.y && absN.x >= absN.z) return pos.yz;
    if (absN.y >= absN.x && absN.y >= absN.z) return pos.xz;
    return pos.xy;
}

// ---------------------------------------------------------------------------
// Vertex shader — reads displaced positions for rendering, base positions
// for UV generation
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Displaced position for rendering (includes wave displacement)
    VertexPosition dvp = displacedPositions[vertexID];
    float3 dispPos = float3(dvp.x, dvp.y, dvp.z);
    float3 normal  = float3(dvp.nx, dvp.ny, dvp.nz);

    // Base (undisplaced) position for stable UV generation
    VertexPosition bvp = basePositions[vertexID];
    float3 basePos = float3(bvp.x, bvp.y, bvp.z);

    float4 worldPos4 = mul(ubo.model, float4(dispPos, 1.0));
    output.worldPos    = worldPos4.xyz;
    output.normal      = normalize(mul((float3x3)ubo.model, normal));
    output.pos         = mul(ubo.proj, mul(ubo.view, worldPos4));
    output.color       = float3(dvp.r, dvp.g, dvp.b);
    output.modelPos    = basePos;  // Undisplaced — fixed to the mesh

    return output;
}

// ---------------------------------------------------------------------------
// Fragment shader – uses geometric face normal from index buffer
// ---------------------------------------------------------------------------
[shader("fragment")]
float4 fragMain(VSOutput input, uint primitiveID : SV_PrimitiveID) : SV_TARGET {
    FaceData face = faceDataBuffer[primitiveID];

    // ---- Compute true geometric face normal from the three vertices ----
    uint baseIndex = primitiveID * 3;
    uint i0 = indexBuffer[baseIndex + 0];
    uint i1 = indexBuffer[baseIndex + 1];
    uint i2 = indexBuffer[baseIndex + 2];

    float3 p0 = float3(basePositions[i0].x, basePositions[i0].y, basePositions[i0].z);
    float3 p1 = float3(basePositions[i1].x, basePositions[i1].y, basePositions[i1].z);
    float3 p2 = float3(basePositions[i2].x, basePositions[i2].y, basePositions[i2].z);

    float3 faceNormal = normalize(cross(p1 - p0, p2 - p0));
    // Ensure the normal points outward (optional, depends on your geometry)
    // faceNormal = dot(faceNormal, p0) < 0 ? -faceNormal : faceNormal;

    // ---- Lighting from smooth vertex normal ----
    float3 lightDir    = normalize(float3(0.3, 1.0, 0.5));
    float  lightFactor = max(dot(normalize(input.normal), lightDir), 0.3);

    // UV: box‑mapped from base object‑space position using the geometric face normal
    float2 uv = boxMapUV(input.modelPos, faceNormal) + float2(0.5, 0.5);

    uint flags = face.effectFlags;
    uint texId = face.textureId;

    // Base colour: textured or vertex colour with lighting
    float4 baseColor;
    if (texId != 0xFFFFFFFF) {
        baseColor = sampleBindlessTexture(texId, uv, pc.time);
    } else {
        baseColor = float4(input.color * lightFactor, 1.0);
    }

    // Apply per‑face effects if any
    if (flags != 0u) {
        float4 fx = dispatchFaceEffect(flags, uv, input.worldPos.xy,
                                       pc.time, face.params[0], face.params[1], 1.0);
        if (texId != 0xFFFFFFFF)
            return lerp(baseColor, fx, 0.5);
        return fx * float4(float3(lightFactor, lightFactor, lightFactor), 1.0);
    }

    return baseColor;
}
