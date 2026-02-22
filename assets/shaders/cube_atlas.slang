// Cube shader - procedural atlas layers (simulates multiple textures from atlas)
// Layers together multiple procedural patterns: stripes, dots, and gradient
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

    pos += waveDisplacement(pos, pc.time);

    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    uint localIdx = vertexID % 6;
    output.uv = faceUVs[localIdx];

    return output;
}

// Procedural atlas layer composition (simulates multiple textures layered)
[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET {
    float2 uv = input.uv;

    // Layer 1: Horizontal stripes (simulating first atlas texture)
    float stripes = step(0.5, fmod(uv.y * 6.0, 1.0));
    float3 stripeColor = lerp(float3(0.2, 0.4, 0.6), float3(0.3, 0.6, 0.8), stripes);

    // Layer 2: Circular dots pattern (simulating second atlas texture)
    float2 dotUV = frac(uv * 4.0) - 0.5;
    float dotDist = length(dotUV);
    float dots = 1.0 - smoothstep(0.15, 0.2, dotDist);
    float3 dotColor = float3(0.9, 0.8, 0.3);  // Gold dots

    // Layer 3: Diagonal gradient (simulating third atlas texture)
    float gradient = (uv.x + uv.y) * 0.5;
    float3 gradColor = lerp(float3(0.1, 0.1, 0.3), float3(0.3, 0.1, 0.4), gradient);

    // Composite: base stripes + dots overlay + gradient tint
    float3 color = stripeColor;
    color = lerp(color, dotColor, dots * 0.7);
    color += gradColor * 0.3;

    return float4(saturate(color), 1.0);
}
