// object_update.slang
// Per-frame compute shader for vertex displacement effects.
//
// Runs once per unique vertex each frame.  For each vertex, scans the
// index buffer to find all triangles that reference it, reads the
// per-face effect data, and accumulates position-modifying effects
// (wave, and any future effects) across ALL adjacent faces.
//
// Adding a new vertex-position effect only requires a new branch here —
// no new CPU code, no new SSBOs, no new bindings.
//
// Workgroup size: 64 threads per group.

import face_effects;

// ---------------------------------------------------------------------------
// Per-vertex position struct (must match device::GPUVertexPosition, 48 bytes)
// ---------------------------------------------------------------------------
struct VertexPosition {
    float x, y, z, w;
    float r, g, b, pad0;
    float nx, ny, nz, npad;
};

// ---------------------------------------------------------------------------
// Bindings (set 0)
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData>          faceData;
[[vk::binding(2, 0)]] StructuredBuffer<VertexPosition>    basePositions;
[[vk::binding(3, 0)]] RWStructuredBuffer<VertexPosition>  displacedPositions;
[[vk::binding(4, 0)]] StructuredBuffer<uint>              indices;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
struct UpdateParams {
    float time;
    uint  objectId;
    uint  vertexCount;
    uint  indexCount;
};
[[vk::push_constant]] ConstantBuffer<UpdateParams> pc;

// ---------------------------------------------------------------------------
// Compute shader entry point
// ---------------------------------------------------------------------------
[shader("compute")]
[numthreads(64, 1, 1)]
void updateMain(uint3 tid : SV_DispatchThreadID) {
    uint vi = tid.x;
    if (vi >= pc.vertexCount) return;

    VertexPosition base = basePositions[vi];
    float3 pos = float3(base.x, base.y, base.z);
    float3 totalDisp = float3(0.0, 0.0, 0.0);

    // Triangle topology: every 3 indices form one face.
    // The index buffer is always a multiple of 3 (enforced by calculateFaces).
    uint faceCount = pc.indexCount / 3u;
    for (uint fi = 0u; fi < faceCount; ++fi) {
        uint i0 = indices[fi * 3u + 0u];
        uint i1 = indices[fi * 3u + 1u];
        uint i2 = indices[fi * 3u + 2u];
        if (i0 != vi && i1 != vi && i2 != vi) continue;

        FaceData face = faceData[fi];

        // Vertex-position effects — add new effects here as needed
        // (no new bindings required)
        if ((face.effectFlags & EFFECT_WAVE) != 0u) {
            totalDisp += waveVertexDisplacement(pos, pc.time,
                                                face.params[0], face.params[1]);
        }
        // Future: if ((face.effectFlags & EFFECT_RIPPLE) != 0u) { ... }
    }

    VertexPosition out = base;
    out.x = base.x + totalDisp.x;
    out.y = base.y + totalDisp.y;
    out.z = base.z + totalDisp.z;
    displacedPositions[vi] = out;
}
