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

// Vertex shader - defer transformation to geometry shader
[shader("vertex")]
VSOutput vertMain(VSInput input) {
    VSOutput output;
    output.pos = float4(input.inPosition, 0.0, 1.0);  // 2D point, no transformation yet
    output.color = input.inColor;
    return output;
}

// Geometry shader - pass through triangles with transformation
// Takes a triangle and applies model-view-projection transformation
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    GSOutput output;

    // Pass through the triangle vertices, applying transformations
    for (int i = 0; i < 3; i++) {
        output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, input[i].pos)));
        output.color = input[i].color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
