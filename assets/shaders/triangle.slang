// Triangle shader - renders a colored triangle using geometry shader
// Vertex shader outputs a point, geometry shader expands to triangle,
// fragment shader colors it.

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
};

// Vertex shader - outputs vertices with hardcoded triangle positions
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

    output.pos = float4(positions[vertexID], 0.0, 1.0);
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
