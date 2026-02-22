// Unified cube shader - single shader supporting per-vertex rendering modes
//
// Each vertex carries a renderMode that selects how the fragment is shaded:
//   Mode 0: Plain uniform color (uses vertex color directly)
//   Mode 1: Procedural checkerboard texture (uses UV coordinates)
//   Mode 2: Procedural atlas layers - stripes, dots, gradient (uses UV)
//   Mode 3: Gradient + wave displacement visualization (uses model position)
//
// All modes share the same wave displacement for vertex consistency.

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

struct VSOutput {
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
    2, 2,  // Back   (-Z): atlas layers
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
// Fragment mode implementations
// ---------------------------------------------------------------------------

// Mode 0: Plain uniform color
float4 modePlainColor(VSOutput input) {
    return float4(input.color, 1.0);
}

// Mode 1: Procedural checkerboard texture
float4 modeCheckerboard(VSOutput input) {
    float2 uv = input.uv;
    float2 grid = floor(uv * 8.0);
    float checker = fmod(grid.x + grid.y, 2.0);

    float3 color1 = float3(0.9, 0.55, 0.2);   // Orange
    float3 color2 = float3(0.95, 0.9, 0.8);    // Cream
    float3 color = lerp(color1, color2, checker);
    return float4(color, 1.0);
}

// Mode 2: Procedural atlas layers (stripes + dots + gradient)
float4 modeAtlasLayers(VSOutput input) {
    float2 uv = input.uv;

    // Layer 1: Horizontal stripes
    float stripes = step(0.5, fmod(uv.y * 6.0, 1.0));
    float3 stripeColor = lerp(float3(0.2, 0.4, 0.6), float3(0.3, 0.6, 0.8), stripes);

    // Layer 2: Circular dots pattern
    float2 dotUV = frac(uv * 4.0) - 0.5;
    float dotDist = length(dotUV);
    float dots = 1.0 - smoothstep(0.15, 0.2, dotDist);
    float3 dotColor = float3(0.9, 0.8, 0.3);  // Gold dots

    // Layer 3: Diagonal gradient
    float gradient = (uv.x + uv.y) * 0.5;
    float3 gradColor = lerp(float3(0.1, 0.1, 0.3), float3(0.3, 0.1, 0.4), gradient);

    // Composite
    float3 color = stripeColor;
    color = lerp(color, dotColor, dots * 0.7);
    color += gradColor * 0.3;
    return float4(saturate(color), 1.0);
}

// Mode 3: Gradient + wave displacement visualization
float4 modeGradientWave(VSOutput input) {
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
float4 fragMain(VSOutput input) : SV_TARGET {
    switch (input.renderMode) {
        case 1:  return modeCheckerboard(input);
        case 2:  return modeAtlasLayers(input);
        case 3:  return modeGradientWave(input);
        default: return modePlainColor(input);
    }
}
