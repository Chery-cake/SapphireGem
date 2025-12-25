// Layered texture shader with animated background
// Adapted from: https://www.shadertoy.com/view/mtyGWy

struct VSInput {
    float3 inPosition;
    float2 inTexCoord;
    float3 inColor;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float time;        // iTime equivalent
    float2 resolution; // iResolution equivalent
    float padding[1];  // Alignment padding
};
ConstantBuffer<UniformBuffer> ubo;

Sampler2D texSampler;

struct VSOutput
{
    float4 pos : SV_Position;
    float2 texCoord;
    float3 color;
    float2 fragCoord;
};

// Palette function from https://iquilezles.org/articles/palettes/
float3 palette(float t) {
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.263, 0.416, 0.557);
    
    return a + b * cos(6.28318 * (c * t + d));
}

[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 1.0))));
    output.texCoord = input.inTexCoord;
    output.color = input.inColor;
    
    // Calculate fragment coordinate for shader effect
    // Convert from clip space to screen space coordinates
    float4 clipPos = output.pos;
    output.fragCoord = (clipPos.xy / clipPos.w) * 0.5 + 0.5;
    output.fragCoord *= ubo.resolution;
    
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput vertIn) : SV_TARGET {
    float2 fragCoord = vertIn.fragCoord;
    
    // Normalized pixel coordinates (from -1 to 1)
    float2 uv = (fragCoord * 2.0 - ubo.resolution) / ubo.resolution.y;
    float2 uv0 = uv;
    float3 finalColor = float3(0.0, 0.0, 0.0);
    
    for (float i = 0.0; i < 4.0; i += 1.0) {
        uv = frac(uv * 1.5) - 0.5;
        
        float d = length(uv) * exp(-length(uv0));
        
        float3 col = palette(length(uv0) + i * 0.4 + ubo.time * 0.4);
        
        d = sin(d * 8.0 + ubo.time) / 8.0;
        d = abs(d);
        
        d = pow(0.01 / d, 1.2);
        
        finalColor += col * d;
    }
    
    // Sample the texture (layered images)
    float4 texColor = texSampler.Sample(vertIn.texCoord);
    
    // Blend the animated background with the texture
    // If texture has alpha, use it for blending
    float3 background = finalColor;
    float3 blended = lerp(background, texColor.rgb, texColor.a);
    
    return float4(blended, 1.0);
}
