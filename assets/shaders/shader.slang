// Basic 3D Shader - Traditional vertex/fragment pipeline
// This is the traditional approach where vertex data contains full geometry
// Compare with:
// - geometry3d.slang: Generates 3D quads from point primitives (2D->3D expansion)
// - mesh3d.slang: Generates geometry procedurally without vertex buffers

struct VSInput {
    float3 inPosition;
    float3 inColor;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput
{
    float4 pos : SV_Position;
    float3 color;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    // Transform 3D position through model-view-projection matrices
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    return float4(vertIn.color, 1.0);
}
