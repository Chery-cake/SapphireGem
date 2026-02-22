// Cube shader - procedural texture (simulates single texture from file)
// Renders a checkerboard pattern on cube faces
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
    float3 modelPos;
    float2 uv;
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

static const uint cubeIndices[36] = {
    0, 1, 2,  0, 2, 3,   // Front  (+Z)
    5, 4, 7,  5, 7, 6,   // Back   (-Z)
    4, 0, 3,  4, 3, 7,   // Left   (-X)
    1, 5, 6,  1, 6, 2,   // Right  (+X)
    3, 2, 6,  3, 6, 7,   // Top    (+Y)
    4, 5, 1,  4, 1, 0,   // Bottom (-Y)
};

// UV coordinates per vertex (2 triangles per face, 6 vertices per face)
static const float2 faceUVs[6] = {
    float2(0.0, 1.0),  // Triangle 1: v0
    float2(1.0, 1.0),  // Triangle 1: v1
    float2(1.0, 0.0),  // Triangle 1: v2
    float2(0.0, 1.0),  // Triangle 2: v0
    float2(1.0, 0.0),  // Triangle 2: v2
    float2(0.0, 0.0),  // Triangle 2: v3
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

    pos += waveDisplacement(pos, pc.time);

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    // UV from local vertex position within the face
    uint localIdx = vertexID % 6;
    output.uv = faceUVs[localIdx];

    return output;
}

// Procedural checkerboard texture (simulates loading a texture from file)
[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float2 uv = input.uv;

    // Checkerboard pattern (8x8 grid)
    float2 grid = floor(uv * 8.0);
    float checker = fmod(grid.x + grid.y, 2.0);

    // Two-tone checkerboard: warm orange and cream
    float3 color1 = float3(0.9, 0.55, 0.2);  // Orange
    float3 color2 = float3(0.95, 0.9, 0.8);  // Cream
    float3 color = lerp(color1, color2, checker);

    return float4(color, 1.0);
}
