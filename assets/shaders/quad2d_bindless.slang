// Bindless 2D quad shader – uses the GPU texture table and bindless
// descriptor arrays for all texture access.
//
// Uses 3x3 matrices for 2D homogeneous coordinate transforms.
// The textureId is passed via push constants and selects a TextureRecord.
//
// Descriptor set 0: per-object UBO + push constants
// Descriptor set 1: global bindless set (images, samplers, SSBOs)

import bindless_common;

struct UniformBuffer {
    float3x3 model;
    float3x3 view;
    float3x3 proj;
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
    float2 modelPos;
    nointerpolation uint textureId;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float2 modelPos;
    nointerpolation uint textureId;
};

// 2x2 grid quad positions (24 vertices)
static const float2 quadPositions[24] = {
    // Face 0 (top-left)
    float2(-0.8,  0.0), float2( 0.0,  0.0), float2( 0.0,  0.8),
    float2(-0.8,  0.0), float2( 0.0,  0.8), float2(-0.8,  0.8),
    // Face 1 (top-right)
    float2( 0.0,  0.0), float2( 0.8,  0.0), float2( 0.8,  0.8),
    float2( 0.0,  0.0), float2( 0.8,  0.8), float2( 0.0,  0.8),
    // Face 2 (bottom-left)
    float2(-0.8, -0.8), float2( 0.0, -0.8), float2( 0.0,  0.0),
    float2(-0.8, -0.8), float2( 0.0,  0.0), float2(-0.8,  0.0),
    // Face 3 (bottom-right)
    float2( 0.0, -0.8), float2( 0.8, -0.8), float2( 0.8,  0.0),
    float2( 0.0, -0.8), float2( 0.8,  0.0), float2( 0.0,  0.0),
};

static const float2 quadUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    float2 pos = quadPositions[vertexID];
    output.modelPos = pos;

    uint uvIdx = vertexID % 6;
    output.uv = quadUVs[uvIdx];

    float3 pos2d  = float3(pos, 1.0);
    float3 worldPos = mul(ubo.model, pos2d);
    float3 viewPos  = mul(ubo.view, worldPos);
    float3 clipPos  = mul(ubo.proj, viewPos);

    output.pos = float4(clipPos.xy, 0.0, 1.0);
    output.color = float3(0.4, 0.6, 0.9);
    output.textureId = pc.textureId;

    return output;
}

// Geometry shader – area-based brightness
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    float2 e1 = input[1].modelPos - input[0].modelPos;
    float2 e2 = input[2].modelPos - input[0].modelPos;
    float area = abs(e1.x * e2.y - e1.y * e2.x) * 0.5;
    float areaFactor = clamp(1.0 - area * 0.3, 0.7, 1.0);

    for (int i = 0; i < 3; i++) {
        GSOutput output;
        output.pos       = input[i].pos;
        output.color     = input[i].color * areaFactor;
        output.uv        = input[i].uv;
        output.modelPos  = input[i].modelPos;
        output.textureId = input[i].textureId;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader – bindless texture sampling
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    float4 texColor = sampleBindlessTexture(input.textureId, input.uv, pc.time);
    texColor.rgb *= input.color;
    return texColor;
}
