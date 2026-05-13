// frustum_cull.slang
// GPU frustum-culling compute pass for instanced indirect rendering.
//
// For each DrawBatch, transforms the batch's local-space AABB using the first
// instance's world transform and tests the resulting world-space AABB against
// the six camera-frustum planes (passed as push constants).  Batches that lie
// fully outside any frustum plane have their instanceCount zeroed in the
// indirect command buffer so the GPU skips the draw entirely.
//
// Frustum planes follow the convention:
//   dot(plane.xyz, worldPos) + plane.w >= 0   ↔   point is inside
// Extract them from the combined view–projection matrix (VP) using the
// Gribb–Hartman method (row vectors of VP):
//   left   = VP_row3 + VP_row0     right  = VP_row3 − VP_row0
//   bottom = VP_row3 + VP_row1     top    = VP_row3 − VP_row1
//   near   = VP_row2               far    = VP_row3 − VP_row2
//
// Binding layout (set 0):
//   0 – StructuredBuffer<GPUBatchDescriptor>  batchDescs   (read-only)
//   1 – StructuredBuffer<GPUInstanceData>     instances    (read-only)
//   2 – RWStructuredBuffer<IndirectCommand>   indirectCmds (read-write)
//
// Push constants: CullParams (112 bytes)
//
// Workgroup size: 64 threads per group.

// ── Structs ───────────────────────────────────────────────────────────────────

struct GPUBatchDescriptor {
    uint  firstIndex;
    uint  indexCount;
    uint  firstInstance;
    uint  instanceCount;
    float boundsMinX; float boundsMinY; float boundsMinZ; float pad0;
    float boundsMaxX; float boundsMaxY; float boundsMaxZ; float pad1;
};

// Must match window::GPUInstanceData (80 bytes).
struct GPUInstanceData {
    row_major float4x4 transform;  // 64 bytes: world-space model matrix
    uint materialId;
    uint meshId;
    uint pad0;
    uint pad1;
};

// Matches VkDrawIndexedIndirectCommand (Vulkan spec §36.1).
struct IndirectCommand {
    uint  indexCount;
    uint  instanceCount;
    uint  firstIndex;
    int   vertexOffset;
    uint  firstInstance;
};

// ── Bindings ──────────────────────────────────────────────────────────────────

[[vk::binding(0, 0)]] StructuredBuffer<GPUBatchDescriptor>  batchDescs;
[[vk::binding(1, 0)]] StructuredBuffer<GPUInstanceData>     instances;
[[vk::binding(2, 0)]] RWStructuredBuffer<IndirectCommand>   indirectCmds;

// ── Push constants (112 bytes) ────────────────────────────────────────────────

struct CullParams {
    uint   batchCount;
    uint   pad0; uint pad1; uint pad2;   // pad to 16-byte boundary
    float4 frustumPlanes[6];             // 6 × 16 = 96 bytes
};
[[vk::push_constant]] ConstantBuffer<CullParams> pc;

// ── Helper: AABB vs frustum ───────────────────────────────────────────────────
// Returns true if the world-space AABB [wMin, wMax] is fully outside at least
// one frustum plane (i.e. the batch is definitely not visible).

bool isAABBCulled(float3 wMin, float3 wMax) {
    for (int p = 0; p < 6; ++p) {
        float4 plane = pc.frustumPlanes[p];
        // Positive vertex: the AABB corner most in the direction of the plane normal.
        float3 pv;
        pv.x = (plane.x >= 0.0f) ? wMax.x : wMin.x;
        pv.y = (plane.y >= 0.0f) ? wMax.y : wMin.y;
        pv.z = (plane.z >= 0.0f) ? wMax.z : wMin.z;
        // dot(n, pv) + d < 0 → entire AABB is on the outer side of this plane.
        if (dot(plane.xyz, pv) + plane.w < 0.0f)
            return true;
    }
    return false;
}

// ── Entry point ───────────────────────────────────────────────────────────────

[shader("compute")]
[numthreads(64, 1, 1)]
void cullMain(uint3 tid : SV_DispatchThreadID) {
    uint bi = tid.x;
    if (bi >= pc.batchCount) return;

    GPUBatchDescriptor desc = batchDescs[bi];
    if (desc.instanceCount == 0u) return;  // Already empty batch.

    // Use the first instance's transform to move the shared AABB to world space.
    GPUInstanceData inst = instances[desc.firstInstance];
    float4x4        M    = inst.transform;

    float3 lo = float3(desc.boundsMinX, desc.boundsMinY, desc.boundsMinZ);
    float3 hi = float3(desc.boundsMaxX, desc.boundsMaxY, desc.boundsMaxZ);

    // Compute world-space AABB by transforming all 8 AABB corners.
    float3 wMin = float3( 1.0e30f,  1.0e30f,  1.0e30f);
    float3 wMax = float3(-1.0e30f, -1.0e30f, -1.0e30f);

    for (int cx = 0; cx < 2; ++cx)
    for (int cy = 0; cy < 2; ++cy)
    for (int cz = 0; cz < 2; ++cz) {
        float3 corner = float3(
            cx == 0 ? lo.x : hi.x,
            cy == 0 ? lo.y : hi.y,
            cz == 0 ? lo.z : hi.z
        );
        float4 world = mul(M, float4(corner, 1.0f));
        wMin = min(wMin, world.xyz);
        wMax = max(wMax, world.xyz);
    }

    if (isAABBCulled(wMin, wMax)) {
        indirectCmds[bi].instanceCount = 0u;
    }
}
