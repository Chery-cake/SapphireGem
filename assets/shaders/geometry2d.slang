// Geometry shader for 2D objects - demonstrates modular rendering
// Takes a single POINT and generates a QUAD (2D primitive)
// Benefits of geometry shaders:
// - Reduced CPU-to-GPU data transfer (send points instead of quads)
// - Dynamic geometry generation on GPU
// - More flexible and modular pipeline

struct VSInput {
    float2 inPosition;  // Just a 2D point
    float3 inColor;     // And its color
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

struct GSOutput
{
    float4 pos : SV_Position;
    float3 color;
};

// Vertex shader - minimal processing
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 0.0, 1.0);  // 2D point, defer transformation
    output.color = input.inColor;
    return output;
}

// Geometry shader - generates quad from point
// This is the modular part: one point becomes multiple primitives
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
    
    // Create a quad centered at the point in 2D space
    float2 corners[4];
    corners[0] = center + float2(-size, -size);
    corners[1] = center + float2( size, -size);
    corners[2] = center + float2( size,  size);
    corners[3] = center + float2(-size,  size);
    
    // First triangle (0, 1, 2)
    for (int i = 0; i < 3; i++) {
        int idx = (i == 0) ? 0 : (i == 1) ? 1 : 2;
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[idx], 0.0, 1.0))));
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
    
    // Second triangle (0, 2, 3)
    int indices[3] = {0, 2, 3};
    for (int i = 0; i < 3; i++) {
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[indices[i]], 0.0, 1.0))));
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
