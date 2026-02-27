// Bindless cube shader – uses the GPU texture table and bindless descriptor
// arrays instead of per-material combined-image-sampler bindings.
//
// Each vertex carries a textureId that selects a TextureRecord from the
// SSBO.  The fragment shader composites layers via sampleBindlessTexture().
// All objects share this pipeline regardless of which textures they use.
//
// Descriptor set 0: per-object UBO + push constants (unchanged)
// Descriptor set 1: global bindless set (images, samplers, SSBOs)

import bindless_common;

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

struct PushData {
    float time;
    uint  textureId;  // TextureId index into TextureRecord SSBO
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
    float waveAmount;
    nointerpolation uint textureId;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
    float waveAmount;
    nointerpolation uint textureId;
};

// 8 cube vertex positions
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

// 36 indices for 12 triangles
static const uint cubeIndices[36] = {
    0, 1, 2,  0, 2, 3,   // Front  (+Z)
    5, 4, 7,  5, 7, 6,   // Back   (-Z)
    4, 0, 3,  4, 3, 7,   // Left   (-X)
    1, 5, 6,  1, 6, 2,   // Right  (+X)
    3, 2, 6,  3, 6, 7,   // Top    (+Y)
    4, 5, 1,  4, 1, 0,   // Bottom (-Y)
};

// UV coordinates per vertex within a face
static const float2 faceUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

float3 waveDisplacement(float3 pos, float t) {
    float wave = sin(pos.x * 4.0 + t * 2.0) * 0.05;
    wave += cos(pos.z * 4.0 + t * 1.5) * 0.05;
    return float3(0.0, wave, 0.0);
}

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    uint idx = cubeIndices[vertexID];
    float3 pos = cubePositions[idx];
    output.modelPos = pos;

    float3 disp = waveDisplacement(pos, pc.time);
    pos += disp;
    output.waveAmount = disp.y;

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos  = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    uint localIdx = vertexID % 6;
    output.uv = faceUVs[localIdx];

    output.color = float3(0.27, 0.51, 0.71);
    output.textureId = pc.textureId;

    return output;
}

// Geometry shader – passes through with face normal lighting
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
        output.pos        = input[i].pos;
        output.color      = input[i].color * lightFactor;
        output.uv         = input[i].uv;
        output.modelPos   = input[i].modelPos;
        output.waveAmount = input[i].waveAmount;
        output.textureId  = input[i].textureId;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader – uses bindless texture sampling
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    // Sample via the GPU texture table
    float4 texColor = sampleBindlessTexture(input.textureId, input.uv, pc.time);

    // Apply lighting
    texColor.rgb *= input.color;

    return texColor;
}
