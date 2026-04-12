// object_base.slang
// Primary shader file for 3D objects with per-face material support.
//
// Uses indexed drawing with shared vertices. The vertex shader transforms
// positions without face-specific data. The geometry shader looks up
// per-face data via SV_PrimitiveID and applies per-face wave displacement,
// ensuring faces with wave effects displace independently without affecting
// non-wave faces or causing seams at shared edges.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 4×4 matrices)
//   binding 1: FaceData[] SSBO (per-face material data)
//   binding 2: VertexPosition[] SSBO (per-vertex positions)
// Descriptor set 1: global bindless set (from bindless_common.slang)

import bindless_common;
import face_effects;

// ---------------------------------------------------------------------------
// UBO: 4x4 matrices for 3D objects
// ---------------------------------------------------------------------------
struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

// ---------------------------------------------------------------------------
// Per-face data SSBO (set 0, binding 1)
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Per-vertex position SSBO (set 0, binding 2)
// Positions are uploaded by the CPU for each Object — no hardcoded geometry.
// ---------------------------------------------------------------------------
struct VertexPosition {
    float x, y, z, w;
};
[[vk::binding(2, 0)]] StructuredBuffer<VertexPosition> vertexPositions;

// ---------------------------------------------------------------------------
// Push constants
// ---------------------------------------------------------------------------
struct PushData {
    float time;
    uint  objectId;
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

// ---------------------------------------------------------------------------
// Shader I/O structs
// ---------------------------------------------------------------------------
struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float3 modelPos;
    nointerpolation uint textureId;
    nointerpolation uint effectFlags;
    nointerpolation float params[2];
};

// UV coordinates per vertex within a face (2 triangles per quad face)
static const float2 faceUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

// ---------------------------------------------------------------------------
// Vertex shader
// ---------------------------------------------------------------------------
[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Read position from the per-object vertex position SSBO
    VertexPosition vp = vertexPositions[vertexID];
    float3 pos = float3(vp.x, vp.y, vp.z);
    output.modelPos = pos;

    // Transform position: model → view → proj
    // No wave displacement here — that is applied per-face in the geometry
    // shader so that shared vertices between wave and non-wave faces are
    // handled correctly.
    float4 worldPos = mul(ubo.model, float4(pos, 1.0));
    float4 viewPos  = mul(ubo.view, worldPos);
    output.pos = mul(ubo.proj, viewPos);

    // UV from the draw-order position within each quad
    // With indexed drawing, vertexID is the vertex buffer index.
    // The geometry shader will assign per-face UVs instead.
    output.uv = float2(0.0, 0.0);

    output.color = float3(0.27, 0.51, 0.71);

    return output;
}

// ---------------------------------------------------------------------------
// Geometry shader – per-face data lookup, wave displacement, lighting
// ---------------------------------------------------------------------------
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    uint primitiveID : SV_PrimitiveID,
    inout TriangleStream<GSOutput> outputStream
) {
    // Look up per-face data using the primitive ID (triangle index)
    FaceData face = faceDataBuffer[primitiveID];

    // Apply per-face wave displacement to vertex positions.
    // Each face displaces its own copy of the vertices independently.
    // Non-wave faces (params={0,0}) get zero displacement.
    float3 positions[3];
    for (int j = 0; j < 3; j++) {
        float3 pos = input[j].modelPos;
        float3 disp = waveVertexDisplacement(pos, pc.time, face.params[0], face.params[1]);
        positions[j] = pos + disp;
    }

    // Compute face normal from (potentially displaced) positions
    float3 edge1 = positions[1] - positions[0];
    float3 edge2 = positions[2] - positions[0];
    float3 faceNormal = normalize(cross(edge1, edge2));

    float3 lightDir = normalize(float3(0.3, 1.0, 0.5));
    float lightFactor = max(dot(faceNormal, lightDir), 0.3);

    // Re-transform displaced positions through model/view/proj
    for (int i = 0; i < 3; i++) {
        GSOutput output;

        float4 worldPos = mul(ubo.model, float4(positions[i], 1.0));
        float4 viewPos  = mul(ubo.view, worldPos);
        output.pos = mul(ubo.proj, viewPos);

        output.color        = input[i].color * lightFactor;
        output.modelPos     = positions[i];
        output.textureId    = face.textureId;
        output.effectFlags  = face.effectFlags;
        output.params[0]    = face.params[0];
        output.params[1]    = face.params[1];

        // UV from draw-order position within each quad (6 verts per quad)
        uint localIdx = (primitiveID % 2) * 3 + uint(i);
        output.uv = faceUVs[localIdx];

        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// ---------------------------------------------------------------------------
// Fragment shader – selects rendering based on per-face data
// ---------------------------------------------------------------------------
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    uint flags = input.effectFlags;
    uint texId = input.textureId;

    // If face has effects, dispatch them
    if (flags != 0) {
        float4 effectColor = dispatchFaceEffect(
            flags, input.uv, input.modelPos.xy, pc.time,
            input.params[0], input.params[1], 1.0);

        // If there's also a texture, blend it
        if (texId != 0xFFFFFFFF) {
            float4 texColor = sampleBindlessTexture(texId, input.uv, pc.time);
            return lerp(texColor, effectColor, 0.5);
        }
        return effectColor;
    }

    // No effects — check for texture
    if (texId != 0xFFFFFFFF) {
        return sampleBindlessTexture(texId, input.uv, pc.time);
    }

    // Plain colour fallback
    return float4(input.color, 1.0);
}
