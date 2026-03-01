// Bindless 2D quad shader – uses the GPU texture table and bindless
// descriptor arrays for all texture access.
//
// Uses 3x3 matrices for 2D homogeneous coordinate transforms.
// The textureId is passed via push constants and selects a TextureRecord.
//
// Rendering modes (per-quadrant):
//   Mode 0: Plain uniform color (top-left)
//   Mode 1: Texture via bindless sampling (top-right)
//   Mode 2: Vertical gradient (bottom-left)
//   Mode 3: Animated wave function (bottom-right)
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
    uint  textureId;       // TextureId index into TextureRecord SSBO
    uint  atlasTextureId;  // TextureId for atlas-layered faces (unused in quad)
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

struct VSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float2 modelPos;
    nointerpolation uint textureId;
};

struct GSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
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

// Per-triangle render mode (8 triangles = 4 faces × 2 triangles each)
static const uint triRenderMode[8] = {
    0, 0,  // Face 0 (top-left):     plain color
    1, 1,  // Face 1 (top-right):    texture
    2, 2,  // Face 2 (bottom-left):  gradient
    3, 3,  // Face 3 (bottom-right): animated wave
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

    uint triIndex = vertexID / 3;
    float2 pos = quadPositions[vertexID];

    output.modelPos = pos;
    output.renderMode = (triIndex < 8) ? triRenderMode[triIndex] : 0u;

    uint uvIdx = vertexID % 6;
    output.uv = quadUVs[uvIdx];

    // Apply wave displacement only to vertices in the wave quadrant
    // (bottom-right, face 3) and its shared boundary vertices.
    // Vertices at x>=0 and y<=0 belong to or border face 3, so
    // neighbouring faces deform smoothly at shared edges/corners.
    if (pos.x >= 0.0 && pos.y <= 0.0) {
        float t = pc.time;
        pos.x += sin(pos.y * 6.0 + t * 2.5) * 0.04;
        pos.y += cos(pos.x * 5.0 + t * 2.0) * 0.04;
    }

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
        output.pos        = input[i].pos;
        output.renderMode = input[i].renderMode;
        output.color      = input[i].color * areaFactor;
        output.uv         = input[i].uv;
        output.modelPos   = input[i].modelPos;
        output.textureId  = input[i].textureId;;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// ---------------------------------------------------------------------------
// Fragment mode implementations
// ---------------------------------------------------------------------------
// Mode 0: Plain color
float4 modePlainColor(GSOutput input) {
    return float4(input.color, 1.0);
}

// Mode 1: Texture via bindless sampling
float4 modeTexture(GSOutput input) {
    float4 texColor = sampleBindlessTexture(input.textureId, input.uv, pc.time);
    return texColor;
}

// Mode 2: Vertical gradient
float4 modeGradient(GSOutput input) {
    float t = (input.modelPos.y + 0.8) / 1.6; // Normalize to [0, 1]

    float3 bottomColor = float3(0.1, 0.0, 0.3);
    float3 topColor = float3(0.9, 0.5, 0.1);
    float3 color = lerp(bottomColor, topColor, t);

    return float4(color, 1.0);
}

// Mode 3: Animated wave function
float4 modeWave(GSOutput input) {
    float2 uv = input.uv;
    float t = pc.time;

    float wave1 = sin(uv.x * 10.0 + t * 3.0) * 0.5 + 0.5;
    float wave2 = cos(uv.y * 8.0 + t * 2.0) * 0.5 + 0.5;
    float wave3 = sin((uv.x + uv.y) * 6.0 + t * 1.5) * 0.5 + 0.5;

    float3 color;
    color.r = wave1 * 0.7 + 0.3;
    color.g = wave2 * 0.5 + 0.2;
    color.b = wave3 * 0.8 + 0.2;

    return float4(saturate(color), 1.0);
}

// Unified fragment shader: selects mode per-triangle
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    switch (input.renderMode) {
        case 1:  return modeTexture(input);
        case 2:  return modeGradient(input);
        case 3:  return modeWave(input);
        default: return modePlainColor(input);
    }
}
