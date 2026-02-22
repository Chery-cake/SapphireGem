// Cube shader - uniform color with shared wave displacement
// Base material for cube faces: renders a flat color
// Includes wave displacement for shared vertex consistency

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
    float3 color;
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

// Shared wave displacement function - must be identical across all cube shaders
// to ensure shared vertices between faces with different materials stay in sync
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

    // Apply shared wave displacement
    pos += waveDisplacement(pos, pc.time);

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    // Uniform color: steel blue
    output.color = float3(0.27, 0.51, 0.71);
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
