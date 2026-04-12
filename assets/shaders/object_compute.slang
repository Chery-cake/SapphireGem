// object_compute.slang
// Static compute shader for pre-computing per-vertex smooth normals.
//
// Runs once at object load time.  For each unique vertex, scans the
// index buffer to find all adjacent triangles, computes their face
// normals (area-weighted), accumulates, normalizes, and writes the
// result into the output position buffer's normal fields.
//
// This replaces the geometry shader's per-triangle normal computation
// with smooth (area-weighted) vertex normals computed in parallel.
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
[[vk::binding(3, 0)]] RWStructuredBuffer<VertexPosition>  outputPositions;
[[vk::binding(4, 0)]] StructuredBuffer<uint>              indices;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
struct ComputeParams {
    float time;
    uint  objectId;
    uint  vertexCount;
    uint  indexCount;
};
[[vk::push_constant]] ConstantBuffer<ComputeParams> pc;

// ---------------------------------------------------------------------------
// Compute shader entry point
// ---------------------------------------------------------------------------
[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint vi = dispatchThreadID.x;
    if (vi >= pc.vertexCount) return;

    VertexPosition base = basePositions[vi];
    float3 normalAccum = float3(0.0, 0.0, 0.0);

    // Triangle topology: every 3 indices form one face.
    // The index buffer is always a multiple of 3 (enforced by calculateFaces).
    uint faceCount = pc.indexCount / 3u;
    for (uint fi = 0u; fi < faceCount; ++fi) {
        uint i0 = indices[fi * 3u + 0u];
        uint i1 = indices[fi * 3u + 1u];
        uint i2 = indices[fi * 3u + 2u];
        if (i0 != vi && i1 != vi && i2 != vi) continue;

        // Read the three vertex positions of this triangle
        VertexPosition v0 = basePositions[i0];
        VertexPosition v1 = basePositions[i1];
        VertexPosition v2 = basePositions[i2];

        float3 p0 = float3(v0.x, v0.y, v0.z);
        float3 p1 = float3(v1.x, v1.y, v1.z);
        float3 p2 = float3(v2.x, v2.y, v2.z);

        // Area-weighted face normal (cross product magnitude = 2 * area)
        float3 e1 = p1 - p0;
        float3 e2 = p2 - p0;
        float3 faceNormal = cross(e1, e2);

        normalAccum += faceNormal; // area-weighted accumulation
    }

    // Normalize the accumulated normal.
    // Epsilon 0.001 avoids division by zero for degenerate geometry.
    // Fallback (0,1,0) = up direction, matching the initial default in GPUVertexPosition.
    float len = length(normalAccum);
    float3 finalNormal = len > 0.001 ? normalAccum / len : float3(0.0, 1.0, 0.0);

    // Write output: copy base position + computed normal
    VertexPosition out = base;
    out.nx = finalNormal.x;
    out.ny = finalNormal.y;
    out.nz = finalNormal.z;
    outputPositions[vi] = out;
}
