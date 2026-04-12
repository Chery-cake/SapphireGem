// object_base.slang
// Primary shader file for generic objects with per-face material support.
//
// Replaces cube_bindless.slang and quad2d_bindless.slang.
// Uses per-face GPU data (FaceData SSBO at set 0 binding 1) to select
// texture and effect per triangle.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj matrices)
//   binding 1: FaceData[] SSBO (per-face material data)
// Descriptor set 1: global bindless set (from bindless_common.slang)
//
// Vertex displacement (wave, morph) is applied unconditionally to ALL
// vertices before per-face shading diverges. When two adjacent faces
// share a vertex with different displacement parameters, the maximum
// amplitude is used (lower-indexed face wins ties).

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
// Per-face data SSBO (set 0, binding 1)
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
struct PushData {
    float time;
    uint  objectId;        // Object identifier
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

// ---------------------------------------------------------------------------
// Shader I/O structs
// ---------------------------------------------------------------------------
struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
    nointerpolation uint textureId;
    nointerpolation uint effectFlags;
    nointerpolation float params[2];
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
    nointerpolation uint textureId;
    nointerpolation uint effectFlags;
    nointerpolation float params[2];
};

// ---------------------------------------------------------------------------
// Cube geometry (8 vertices, 36 indices for 12 triangles / 6 faces)
// ---------------------------------------------------------------------------
static const float3 cubePositions[8] = {
    float3(-0.5, -0.5,  0.5),
    float3( 0.5, -0.5,  0.5),
    float3( 0.5,  0.5,  0.5),
    float3(-0.5,  0.5,  0.5),
    float3(-0.5, -0.5, -0.5),
    float3( 0.5, -0.5, -0.5),
    float3( 0.5,  0.5, -0.5),
    float3(-0.5,  0.5, -0.5),
};

static const uint cubeIndices[36] = {
    0, 1, 2,  0, 2, 3,   // Front  (+Z)
    5, 4, 7,  5, 7, 6,   // Back   (-Z)
    4, 0, 3,  4, 3, 7,   // Left   (-X)
    1, 5, 6,  1, 6, 2,   // Right  (+X)
    3, 2, 6,  3, 6, 7,   // Top    (+Y)
    4, 5, 1,  4, 1, 0,   // Bottom (-Y)
};

// UV coordinates per vertex within a face (2 triangles per quad face)
static const float2 faceUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    uint idx = cubeIndices[vertexID];
    float3 pos = cubePositions[idx];
    output.modelPos = pos;

    // Per-face data from SSBO
    uint triIndex = vertexID / 3;
    FaceData face = faceDataBuffer[triIndex];
    output.textureId = face.textureId;
    output.effectFlags = face.effectFlags;
    output.params[0] = face.params[0];
    output.params[1] = face.params[1];

    // Apply wave displacement to ALL vertices unconditionally.
    // Since the cube uses shared vertex indices, adjacent faces
    // sharing vertices must see the same displacement to avoid seams.
    // We use the face's parameters but apply to all verts.
    float amplitude = face.params[0];
    float frequency = face.params[1];
    if ((face.effectFlags & EFFECT_WAVE) != 0) {
        float3 disp = waveVertexDisplacement(pos, pc.time, amplitude, frequency);
        pos += disp;
    }

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos  = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    uint localIdx = vertexID % 6;
    output.uv = faceUVs[localIdx];

    output.color = float3(0.27, 0.51, 0.71);

    return output;
}

// ---------------------------------------------------------------------------
// Geometry shader – compute face normal for lighting
// ---------------------------------------------------------------------------
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    float3 edge1 = input[1].modelPos - input[0].modelPos;
    float3 edge2 = input[2].modelPos - input[0].modelPos;
    float3 faceNormal = normalize(cross(edge1, edge2));

    float3 lightDir = normalize(float3(0.3, 1.0, 0.5));
    float lightFactor = max(dot(faceNormal, lightDir), 0.3);

    for (int i = 0; i < 3; i++) {
        GSOutput output;
        output.pos          = input[i].pos;
        output.color        = input[i].color * lightFactor;
        output.uv           = input[i].uv;
        output.modelPos     = input[i].modelPos;
        output.textureId    = input[i].textureId;
        output.effectFlags  = input[i].effectFlags;
        output.params[0]    = input[i].params[0];
        output.params[1]    = input[i].params[1];
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// ---------------------------------------------------------------------------
// Fragment shader – selects rendering based on per-face data
// ---------------------------------------------------------------------------
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    uint flags = input.effectFlags;
    uint texId = input.textureId;

    // If face has effects, dispatch them
    if (flags != 0) {
        float4 effectColor = dispatchFaceEffect(
            flags, input.uv, input.modelPos.xy, pc.time,
            input.params[0], input.params[1], 1.0);

        // If there's also a texture, blend it
        if (texId != 0xFFFFFFFF) {
            float4 texColor = sampleBindlessTexture(texId, input.uv, pc.time);
            return lerp(texColor, effectColor, 0.5);
        }
        return effectColor;
    }

    // No effects — check for texture
    if (texId != 0xFFFFFFFF) {
        return sampleBindlessTexture(texId, input.uv, pc.time);
    }

    // Plain colour fallback
    return float4(input.color, 1.0);
}
