// Mesh shader for 2D objects - generates 2D quads procedurally
// Demonstrates modern mesh shader pipeline for 2D rendering

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct VertexOutput {
    float4 pos : SV_Position;
    float3 color;
};

// Task shader - determines workload distribution for 2D objects
[shader("task")]
[numthreads(1, 1, 1)]
void taskMain(
    uint3 dispatchThreadID : SV_DispatchThreadID
)
{
    // Simple 1:1 mapping - each task spawns one mesh shader invocation
    DispatchMesh(1, 1, 1, 0);
}

// Mesh shader - generates 2D quad procedurally
[shader("mesh")]
[outputtopology("triangle")]
[numthreads(1, 1, 1)]
void meshMain(
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID,
    out vertices VertexOutput verts[4],
    out indices uint3 tris[2]
)
{
    // Define a quad centered at origin in 2D space
    float size = 0.05;
    
    // Default position (could come from structured buffer in real implementation)
    float2 center = float2(0.0, 0.0);
    float3 color = float3(0.2, 0.8, 0.5);
    
    // Generate 4 vertices for a 2D quad
    float2 positions[4];
    positions[0] = center + float2(-size, -size);
    positions[1] = center + float2( size, -size);
    positions[2] = center + float2( size,  size);
    positions[3] = center + float2(-size,  size);
    
    // Transform and output vertices
    for (int i = 0; i < 4; i++) {
        verts[i].pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(positions[i], 0.0, 1.0))));
        verts[i].color = color;
    }
    
    // Define triangles (indices)
    tris[0] = uint3(0, 1, 2);
    tris[1] = uint3(0, 2, 3);
    
    // Set mesh output counts
    SetMeshOutputCounts(4, 2);
}

// Fragment shader
[shader("fragment")]
float4 fragMain(VertexOutput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
