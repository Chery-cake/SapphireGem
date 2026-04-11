// object_compute.slang
// Compute shader for pre-computing object geometry.
//
// Receives raw vertex positions and face topology as input SSBOs,
// writes out transformed, lighting-pre-baked ProcessedVertex[] into
// device-local memory. This runs once at object load time.
//
// Workgroup size: 64 threads per group.

import face_effects;

struct RawVertex {
    float3 position;
    float3 color;
    float2 uv;
};

struct ProcessedVertex {
    float4 position;    // Object-space position (not yet view/proj transformed)
    float3 color;
    float2 uv;
    float3 normal;      // Pre-computed face normal
    float  pad0;
};

struct ComputeParams {
    uint vertexCount;
    uint faceCount;
    float time;
    uint flags;
};

[[vk::binding(0, 0)]] StructuredBuffer<RawVertex> inputVertices;
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceData;
[[vk::binding(2, 0)]] RWStructuredBuffer<ProcessedVertex> outputVertices;
[[vk::binding(3, 0)]] ConstantBuffer<ComputeParams> params;

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint vertexIdx = dispatchThreadID.x;
    if (vertexIdx >= params.vertexCount) {
        return;
    }

    RawVertex raw = inputVertices[vertexIdx];
    ProcessedVertex out;
    out.position = float4(raw.position, 1.0);
    out.color = raw.color;
    out.uv = raw.uv;
    out.normal = float3(0.0, 1.0, 0.0); // Will be computed per-face
    out.pad0 = 0.0;

    // Compute face normal if this vertex starts a triangle
    uint triIndex = vertexIdx / 3;
    uint localIdx = vertexIdx % 3;

    if (localIdx == 0 && (triIndex * 3 + 2) < params.vertexCount) {
        float3 p0 = inputVertices[triIndex * 3 + 0].position;
        float3 p1 = inputVertices[triIndex * 3 + 1].position;
        float3 p2 = inputVertices[triIndex * 3 + 2].position;
        float3 e1 = p1 - p0;
        float3 e2 = p2 - p0;
        out.normal = normalize(cross(e1, e2));
    }

    // Apply per-face effects (wave displacement)
    if (triIndex < params.faceCount) {
        FaceData face = faceData[triIndex];
        if ((face.effectFlags & EFFECT_WAVE) != 0) {
            float3 disp = waveVertexDisplacement(
                raw.position, params.time, face.effectParam0, face.effectParam1);
            out.position.xyz += disp;
        }
    }

    outputVertices[vertexIdx] = out;
}
