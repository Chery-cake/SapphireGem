// Cube shader - gradient color with wave vertex displacement
// The wave displacement is the primary visual feature of this material
// Fragment shader shows a gradient based on model-space position
// Includes shared wave displacement for vertex consistency

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
    float waveAmount;
};

// 8 cube vertex positions (model space, centered at origin)
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
    0, 1, 2,  0, 2, 3,
    5, 4, 7,  5, 7, 6,
    4, 0, 3,  4, 3, 7,
    1, 5, 6,  1, 6, 2,
    3, 2, 6,  3, 6, 7,
    4, 5, 1,  4, 1, 0,
};

// Shared wave displacement function - identical across all cube shaders
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

    return output;
}

// Gradient fragment shader with wave visualization
[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    // Vertical gradient based on model-space Y position
    float t = input.modelPos.y + 0.5;  // Map [-0.5, 0.5] to [0, 1]

    // Gradient from deep purple at bottom to bright cyan at top
    float3 bottomColor = float3(0.3, 0.0, 0.5);
    float3 topColor = float3(0.0, 0.8, 0.9);
    float3 color = lerp(bottomColor, topColor, t);

    // Add wave displacement visualization as brightness variation
    float waveBrightness = input.waveAmount * 5.0 + 0.5;
    color *= waveBrightness;

    return float4(saturate(color), 1.0);
}
