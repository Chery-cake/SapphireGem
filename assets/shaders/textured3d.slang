struct VSInput {
    float3 inPosition;
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
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.texCoord = input.inTexCoord;
    output.color = input.inColor;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float4 texColor = texSampler.Sample(vertIn.texCoord);
    return texColor * float4(vertIn.color, 1.0);
}
