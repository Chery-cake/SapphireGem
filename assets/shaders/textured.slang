// 2D Textured Shader - Traditional vertex/fragment pipeline
// This demonstrates the traditional approach for 2D rendering
// Compare with:
// - geometry2d_textured.slang: Generates textured quads from point primitives
// - mesh2d.slang: Generates 2D geometry procedurally

struct VSInput {
    float2 inPosition;
    float2 inTexCoord;
    float3 inColor;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

Sampler2D texSampler;

struct VSOutput
{
    float4 pos : SV_Position;
    float2 texCoord;
    float3 color;
};

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    // 2D position with z=0
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 0.0, 1.0))));
    output.texCoord = input.inTexCoord;
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 texColor = texSampler.Sample(vertIn.texCoord);
    // Multiply RGB by vertex color, preserve alpha from texture
    float3 finalColor = texColor.rgb * vertIn.color;
    return float4(finalColor, texColor.a);
}
