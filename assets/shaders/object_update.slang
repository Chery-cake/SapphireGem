// object_update.slang
// Lightweight per-frame compute shader for animated faces only.
//
// Reads the pre-computed geometry buffer and writes an animated overlay
// for faces with effectFlags != 0. Only processes vertices belonging
// to animated faces, leaving static geometry untouched.
//
// Workgroup size: 64 threads per group.

import face_effects;

struct ProcessedVertex {
    float4 position;
    float3 color;
    float2 uv;
    float3 normal;
    float  pad0;
};

struct UpdateParams {
    uint vertexCount;
    uint faceCount;
    float time;
    float deltaTime;
};

[[vk::binding(0, 0)]] StructuredBuffer<ProcessedVertex> baseVertices;
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceData;
[[vk::binding(2, 0)]] RWStructuredBuffer<ProcessedVertex> animatedVertices;
[[vk::binding(3, 0)]] ConstantBuffer<UpdateParams> params;

[shader("compute")]
[numthreads(64, 1, 1)]
void updateMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint vertexIdx = dispatchThreadID.x;
    if (vertexIdx >= params.vertexCount) {
        return;
    }

    uint triIndex = vertexIdx / 3;

    // Copy base vertex first
    ProcessedVertex vert = baseVertices[vertexIdx];

    // Only update vertices belonging to animated faces
    if (triIndex < params.faceCount) {
        FaceData face = faceData[triIndex];
        // Apply wave displacement using per-face params.
        // TODO: Use global wave amplitude/frequency via UpdateParams
        // instead of per-face params to match the rasterization path fix
        // and ensure shared vertices get identical displacement.
        float3 disp = waveVertexDisplacement(
            vert.position.xyz, params.time,
            face.params[0], face.params[1]);
        vert.position.xyz += disp;
    }

    animatedVertices[vertexIdx] = vert;
}
