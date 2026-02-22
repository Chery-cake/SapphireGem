// Unified cube shader - single shader supporting per-vertex rendering modes
// with geometry shader for GPU-side face processing
//
// Each vertex carries a renderMode that selects how the fragment is shaded:
//   Mode 0: Plain uniform color (uses vertex color directly)
//   Mode 1: Checkerboard texture (sampled from bound texture slot 0)
//   Mode 2: Layered atlas texture (sampled from bound texture slot 1)
//   Mode 3: Gradient + wave displacement visualization (uses model position)
//
// Textures are bound per-face from the Object and passed to the Material
// pipeline via combined image sampler descriptors.

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct PushData {
    float time;
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

// Texture samplers bound from Object's per-face texture assignments
// Slot 0: checkerboard texture (checkerboard.png)
// Slot 1: layered atlas texture (layer_atlas.png)
[[vk::binding(1, 0)]] Sampler2D checkerboardTex;
[[vk::binding(2, 0)]] Sampler2D atlasLayerTex;

struct VSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float3 modelPos;
    float waveAmount;
};

struct GSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float3 modelPos;
    float waveAmount;
};

// 8 cube vertex positions (model space, centered at origin)
static const float3 cubePositions[8] = {
    float3(-0.5, -0.5,  0.5),  // 0: front-bottom-left
    float3( 0.5, -0.5,  0.5),  // 1: front-bottom-right
    float3( 0.5,  0.5,  0.5),  // 2: front-top-right
    float3(-0.5,  0.5,  0.5),  // 3: front-top-left
    float3(-0.5, -0.5, -0.5),  // 4: back-bottom-left
    float3( 0.5, -0.5, -0.5),  // 5: back-bottom-right
    float3( 0.5,  0.5, -0.5),  // 6: back-top-right
    float3(-0.5,  0.5, -0.5),  // 7: back-top-left
};

// 36 indices for 12 triangles (6 cube faces, 2 triangles each)
// Winding order: counter-clockwise when viewed from outside
static const uint cubeIndices[36] = {
    0, 1, 2,  0, 2, 3,   // Front  (+Z)
    5, 4, 7,  5, 7, 6,   // Back   (-Z)
    4, 0, 3,  4, 3, 7,   // Left   (-X)
    1, 5, 6,  1, 6, 2,   // Right  (+X)
    3, 2, 6,  3, 6, 7,   // Top    (+Y)
    4, 5, 1,  4, 1, 0,   // Bottom (-Y)
};

// Per-face render mode assignment (6 quad faces × 2 triangles each = 12 entries)
// Index by triangle index (vertexID / 3)
static const uint faceRenderMode[12] = {
    1, 1,  // Front  (+Z): checkerboard texture
    2, 2,  // Back   (-Z): atlas layer texture
    3, 3,  // Left   (-X): gradient + wave
    3, 3,  // Right  (+X): gradient + wave
    0, 0,  // Top    (+Y): plain color
    0, 0,  // Bottom (-Y): plain color
};

// UV coordinates per vertex within a face (2 triangles per face, 6 vertices)
static const float2 faceUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

// Shared wave displacement function - applied to all vertices for consistency
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

    // Apply shared wave displacement
    float3 disp = waveDisplacement(pos, pc.time);
    pos += disp;
    output.waveAmount = disp.y;

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    // Per-face render mode
    uint triIndex = vertexID / 3;
    output.renderMode = faceRenderMode[triIndex];

    // UV coordinates (used by texture/atlas modes)
    uint localIdx = vertexID % 6;
    output.uv = faceUVs[localIdx];

    // Default color: steel blue (used by plain color mode)
    output.color = float3(0.27, 0.51, 0.71);

    return output;
}

// ---------------------------------------------------------------------------
// Geometry shader - passes through triangle vertices with face-level processing
// Computes face normal on GPU for potential lighting calculations
// ---------------------------------------------------------------------------
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    // Compute face normal from triangle edges (GPU-side calculation)
    float3 edge1 = input[1].modelPos - input[0].modelPos;
    float3 edge2 = input[2].modelPos - input[0].modelPos;
    float3 faceNormal = normalize(cross(edge1, edge2));

    // Simple directional light factor based on face normal
    float3 lightDir = normalize(float3(0.3, 1.0, 0.5));
    float lightFactor = max(dot(faceNormal, lightDir), 0.3);

    for (int i = 0; i < 3; i++) {
        GSOutput output;
        output.pos = input[i].pos;
        output.renderMode = input[i].renderMode;
        output.color = input[i].color * lightFactor;
        output.uv = input[i].uv;
        output.modelPos = input[i].modelPos;
        output.waveAmount = input[i].waveAmount;
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

// Mode 1: Checkerboard texture from bound texture slot 0
float4 modeCheckerboard(GSOutput input) {
    float4 texColor = checkerboardTex.Sample(input.uv);
    return texColor;
}

// Mode 2: Layered atlas texture from bound texture slot 1
float4 modeAtlasLayers(GSOutput input) {
    float4 texColor = atlasLayerTex.Sample(input.uv);
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
        case 1:  return modeCheckerboard(input);
        case 2:  return modeAtlasLayers(input);
        case 3:  return modeGradientWave(input);
        default: return modePlainColor(input);
    }
}
