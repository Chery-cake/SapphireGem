// Bindless cube shader – uses the GPU texture table and bindless descriptor
// arrays instead of per-material combined-image-sampler bindings.
//
// Each vertex carries a renderMode that selects how the fragment is shaded:
//   Mode 0: Plain uniform color (uses vertex color directly)
//   Mode 1: Texture from file (sampled via bindless image2D)
//   Mode 2: Layered atlas texture (sampled via bindless atlas layers)
//   Mode 3: Gradient + wave displacement visualization (uses model position)
//
// Face assignment:
//   Front  (+Z): Mode 1 – texture from file
//   Back   (-Z): Mode 2 – layered atlas texture
//   Left   (-X): Mode 3 – gradient + wave
//   Right  (+X): Mode 3 – gradient + wave
//   Top    (+Y): Mode 0 – plain color
//   Bottom (-Y): Mode 0 – plain color
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
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float3 modelPos;
    float waveAmount;
    nointerpolation uint textureId;
};

struct GSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
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

// Per-face render mode (6 faces × 2 triangles each = 12 entries)
static const uint faceRenderMode[12] = {
    1, 1,  // Front  (+Z): texture from file
    2, 2,  // Back   (-Z): atlas layer texture
    3, 3,  // Left   (-X): gradient + wave
    3, 3,  // Right  (+X): gradient + wave
    0, 0,  // Top    (+Y): plain color
    0, 0,  // Bottom (-Y): plain color
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

// Wave displacement applied to gradient faces (mode 3)
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

    // Per-face render mode
    uint triIndex = vertexID / 3;
    output.renderMode = faceRenderMode[triIndex];

    // Apply wave displacement only to gradient+wave faces (mode 3)
    float3 disp = float3(0.0, 0.0, 0.0);
    if (output.renderMode == 3u) {
        disp = waveDisplacement(pos, pc.time);
        pos += disp;
    }
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
        output.renderMode = input[i].renderMode;
        output.color      = input[i].color * lightFactor;
        output.uv         = input[i].uv;
        output.modelPos   = input[i].modelPos;
        output.waveAmount = input[i].waveAmount;
        output.textureId  = input[i].textureId;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// ---------------------------------------------------------------------------
// Fragment mode implementations
// ---------------------------------------------------------------------------

// Mode 0: Plain uniform color
float4 modePlainColor(GSOutput input) {
    return float4(input.color, 1.0);
}

// Mode 1: Texture from file via bindless sampling
float4 modeTexture(GSOutput input) {
    float4 texColor = sampleBindlessTexture(input.textureId, input.uv, pc.time);
    return texColor;
}

// Mode 2: Layered atlas texture via bindless sampling
float4 modeAtlasLayers(GSOutput input) {
    float4 texColor = sampleBindlessTexture(input.textureId, input.uv, pc.time);
    return texColor;
}

// Mode 3: Gradient + wave displacement visualization
float4 modeGradientWave(GSOutput input) {
    float t = input.modelPos.y + 0.5;  // Map [-0.5, 0.5] to [0, 1]

    float3 bottomColor = float3(0.3, 0.0, 0.5);
    float3 topColor = float3(0.0, 0.8, 0.9);
    float3 color = lerp(bottomColor, topColor, t);

    float waveBrightness = input.waveAmount * 5.0 + 0.5;
    color *= waveBrightness;
    return float4(saturate(color), 1.0);
}

// Unified fragment shader: selects mode per-vertex
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    switch (input.renderMode) {
        case 1:  return modeTexture(input);
        case 2:  return modeAtlasLayers(input);
        case 3:  return modeGradientWave(input);
        default: return modePlainColor(input);
    }
}
