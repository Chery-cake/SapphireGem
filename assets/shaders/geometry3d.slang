// Geometry shader that generates 3D objects from 2D primitives
// Takes a single point and generates a quad (2 triangles) in 3D space

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

struct GSOutput
{
    float4 pos : SV_Position;
    float3 color;
};

// Vertex shader - just pass through position and color
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 1.0);
    output.color = input.inColor;
    return output;
}

// Geometry shader - expand point to quad (2D primitive to 3D)
[shader("geometry")]
[maxvertexcount(6)]
void geomMain(
    point VSOutput input[1],
    inout TriangleStream<GSOutput> outputStream
) {
    GSOutput output;
    
    // Get base position and color from input point
    float3 center = input[0].pos.xyz;
    float3 color = input[0].color;
    
    // Define quad size
    float size = 0.1;
    
    // Create a quad centered at the point
    // Define 4 corners of the quad in local space
    float3 corners[4];
    corners[0] = center + float3(-size, -size, 0.0);
    corners[1] = center + float3( size, -size, 0.0);
    corners[2] = center + float3( size,  size, 0.0);
    corners[3] = center + float3(-size,  size, 0.0);
    
    // First triangle (0, 1, 2)
    for (int i = 0; i < 3; i++) {
        int idx = (i == 0) ? 0 : (i == 1) ? 1 : 2;
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[idx], 1.0))));
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
    
    // Second triangle (0, 2, 3)
    int indices[3] = {0, 2, 3};
    for (int i = 0; i < 3; i++) {
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[indices[i]], 1.0))));
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
