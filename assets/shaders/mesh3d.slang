// Mesh shader that generates 3D cubes procedurally from minimal input
// Demonstrates modern mesh shader pipeline where geometry is generated on GPU

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    // Note: time field reserved for future animation support
};
ConstantBuffer<UniformBuffer> ubo;

struct MeshInput {
    float3 center;
    float3 color;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float3 color;
};

// Task shader - determines workload distribution
// Each task generates work for mesh shader
[shader("task")]
[numthreads(1, 1, 1)]
void taskMain(
    uint3 dispatchThreadID : SV_DispatchThreadID
)
{
    // For now, simple 1:1 mapping - each task spawns one mesh shader invocation
    // In more complex scenarios, this could do culling, LOD selection, etc.
    DispatchMesh(1, 1, 1, 0);
}

// Mesh shader - generates quad (2 triangles) procedurally
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
    // Define a quad centered at origin
    float size = 0.1;
    
    // Default position (could come from structured buffer in real implementation)
    float3 center = float3(0.0, 0.0, 0.0);
    float3 color = float3(1.0, 0.5, 0.2);
    
    // Generate 4 vertices for a quad
    float3 positions[4];
    positions[0] = center + float3(-size, -size, 0.0);
    positions[1] = center + float3( size, -size, 0.0);
    positions[2] = center + float3( size,  size, 0.0);
    positions[3] = center + float3(-size,  size, 0.0);
    
    // Transform and output vertices
    for (int i = 0; i < 4; i++) {
        verts[i].pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(positions[i], 1.0))));
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
