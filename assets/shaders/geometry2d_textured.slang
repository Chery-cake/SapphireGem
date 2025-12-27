// Geometry shader for textured 2D objects - generates textured quads from points

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

struct GSOutput
{
    float4 pos : SV_Position;
    float2 texCoord;
    float3 color;
};

// Vertex shader - just pass through
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 0.0, 1.0);
    output.texCoord = input.inTexCoord;
    output.color = input.inColor;
    return output;
}

// Geometry shader - expand point to textured quad
[shader("geometry")]
[maxvertexcount(6)]
void geomMain(
    point VSOutput input[1],
    inout TriangleStream<GSOutput> outputStream
) {
    GSOutput output;
    
    // Get base position and color from input point
    float2 center = input[0].pos.xy;
    float3 color = input[0].color;
    
    // Define quad size
    float size = 0.05;
    
    // Create a quad centered at the point
    float2 corners[4];
    corners[0] = center + float2(-size, -size);
    corners[1] = center + float2( size, -size);
    corners[2] = center + float2( size,  size);
    corners[3] = center + float2(-size,  size);
    
    // Texture coordinates for the quad
    float2 texCoords[4];
    texCoords[0] = float2(0.0, 0.0);
    texCoords[1] = float2(1.0, 0.0);
    texCoords[2] = float2(1.0, 1.0);
    texCoords[3] = float2(0.0, 1.0);
    
    // First triangle (0, 1, 2)
    for (int i = 0; i < 3; i++) {
        int idx = (i == 0) ? 0 : (i == 1) ? 1 : 2;
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[idx], 0.0, 1.0))));
        output.texCoord = texCoords[idx];
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
    
    // Second triangle (0, 2, 3)
    int indices[3] = {0, 2, 3};
    for (int i = 0; i < 3; i++) {
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[indices[i]], 0.0, 1.0))));
        output.texCoord = texCoords[indices[i]];
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    float4 texColor = texSampler.Sample(input.texCoord);
    float3 finalColor = texColor.rgb * input.color;
    return float4(finalColor, texColor.a);
}
