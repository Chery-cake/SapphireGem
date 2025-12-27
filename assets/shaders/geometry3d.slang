// Geometry shader for 3D objects - demonstrates composing 3D from 2D primitives
// Takes a single POINT (0D primitive) and generates a QUAD (2D primitive) in 3D space
// This shows how geometry shaders can build complex geometry from minimal input
// Benefits:
// - Reduced vertex data transfer (only send points, not full quads)
// - Dynamic geometry generation on GPU
// - More modular and flexible rendering pipeline

struct VSInput {
    float3 inPosition;  // Just a point position
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

// Vertex shader - minimal work, just pass through data
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 1.0);  // No transformation yet
    output.color = input.inColor;
    return output;
}

// Geometry shader - the magic happens here!
// Takes a POINT and generates a QUAD (2D surface in 3D space)
// This is where we compose 3D objects from 2D primitives
[shader("geometry")]
[maxvertexcount(6)]  // 2 triangles = 6 vertices
void geomMain(
    point VSOutput input[1],  // Input: single point
    inout TriangleStream<GSOutput> outputStream  // Output: triangle stream
) {
    GSOutput output;
    
    // Get base position and color from input point
    float3 center = input[0].pos.xyz;
    float3 color = input[0].color;
    
    // Define quad size - this could be dynamic based on point attributes
    float size = 0.1;
    
    // Generate 4 corners of a 2D quad in 3D space
    // This demonstrates composing 3D geometry from 2D primitive (quad)
    float3 corners[4];
    corners[0] = center + float3(-size, -size, 0.0);  // Bottom-left
    corners[1] = center + float3( size, -size, 0.0);  // Bottom-right
    corners[2] = center + float3( size,  size, 0.0);  // Top-right
    corners[3] = center + float3(-size,  size, 0.0);  // Top-left
    
    // Emit first triangle (0, 1, 2)
    for (int i = 0; i < 3; i++) {
        int idx = (i == 0) ? 0 : (i == 1) ? 1 : 2;
        // Apply transformations here, after geometry generation
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[idx], 1.0))));
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
    
    // Emit second triangle (0, 2, 3)
    int indices[3] = {0, 2, 3};
    for (int i = 0; i < 3; i++) {
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(corners[indices[i]], 1.0))));
        output.color = color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader - simple color output
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
