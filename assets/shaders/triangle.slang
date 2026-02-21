// Triangle shader - renders a colored triangle using geometry shader
// Supports both 2D and 3D rendering via dimension-appropriate UBO.
// For 2D objects: UBO contains 3x3 matrices (model, view, proj)
// For 3D objects: UBO contains 4x4 matrices (model, view, proj)
//
// This shader uses 4x4 matrices to be compatible with both dimensions.
// The Object<Dim> class uploads the appropriately-sized UBO, and the
// shader reads what it needs.

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
};

// Vertex shader - outputs vertices with hardcoded triangle positions
// Transforms positions using the UBO matrices
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Hardcoded triangle vertices in clip space (Vulkan NDC: +Y is down)
    float2 positions[3];
    positions[0] = float2( 0.0, -0.5);  // Top
    positions[1] = float2( 0.5,  0.5);  // Bottom-right
    positions[2] = float2(-0.5,  0.5);  // Bottom-left

    float3 colors[3];
    colors[0] = float3(1.0, 0.0, 0.0);  // Red
    colors[1] = float3(0.0, 1.0, 0.0);  // Green
    colors[2] = float3(0.0, 0.0, 1.0);  // Blue

    float4 worldPos = mul(ubo.model, float4(positions[vertexID], 0.0, 1.0));
    float4 viewPos = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);
    output.color = colors[vertexID];
    return output;
}

// Geometry shader - passes through triangle vertices
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    for (int i = 0; i < 3; i++) {
        GSOutput output;
        output.pos = input[i].pos;
        output.color = input[i].color;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// Fragment shader - outputs interpolated color
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
