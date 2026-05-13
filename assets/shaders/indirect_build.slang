// indirect_build.slang
// Compute shader: initialises one VkDrawIndexedIndirectCommand per DrawBatch
// from the persistent batch-descriptor table.
//
// This pass runs once per frame (before frustum_cull.slang) to restore the
// full instanceCount in every command slot.  The subsequent culling pass may
// zero instanceCount for batches that fall outside the camera frustum.
//
// Binding layout (set 0):
//   0 – StructuredBuffer<GPUBatchDescriptor>  batchDescs   (read-only)
//   1 – RWStructuredBuffer<IndirectCommand>   indirectCmds (write)
//
// Push constants: BuildParams { batchCount : uint, pad[3] : uint }
//
// Workgroup size: 64 threads per group.

// ── Structs (must match CPU-side GPUBatchDescriptor / VkDrawIndexedIndirectCommand) ──

struct GPUBatchDescriptor {
    uint  firstIndex;
    uint  indexCount;
    uint  firstInstance;
    uint  instanceCount;
    float boundsMinX; float boundsMinY; float boundsMinZ; float pad0;
    float boundsMaxX; float boundsMaxY; float boundsMaxZ; float pad1;
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
[[vk::binding(1, 0)]] RWStructuredBuffer<IndirectCommand>   indirectCmds;

// ── Push constants ────────────────────────────────────────────────────────────

struct BuildParams {
    uint batchCount;
    uint pad0; uint pad1; uint pad2;
};
[[vk::push_constant]] ConstantBuffer<BuildParams> pc;

// ── Entry point ───────────────────────────────────────────────────────────────

[shader("compute")]
[numthreads(64, 1, 1)]
void buildMain(uint3 tid : SV_DispatchThreadID) {
    uint bi = tid.x;
    if (bi >= pc.batchCount) return;

    GPUBatchDescriptor desc = batchDescs[bi];

    IndirectCommand cmd;
    cmd.indexCount    = desc.indexCount;
    cmd.instanceCount = desc.instanceCount;
    cmd.firstIndex    = desc.firstIndex;
    cmd.vertexOffset  = 0;
    cmd.firstInstance = desc.firstInstance;

    indirectCmds[bi] = cmd;
}
