// object_base_2d.slang
// Shader file for 2D objects with per-face material support.
//
// Uses indexed drawing with shared vertices. The vertex shader transforms
// positions without face-specific data. The geometry shader looks up
// per-face data via SV_PrimitiveID and applies per-face wave displacement.
//
// Descriptor set 0:
//   binding 0: per-object UBO (model/view/proj 3×3 matrices, std140 padded)
//   binding 1: FaceData[] SSBO (per-face material data)
//   binding 2: VertexPosition[] SSBO (per-vertex 2D positions, stored as float4)
// Descriptor set 1: global bindless set (from bindless_common.slang)
//
// Depth is hardcoded to 0.0 (front), w=1.0 for clip output.

import bindless_common;
import face_effects;

// ---------------------------------------------------------------------------
// UBO: 3×3 matrices for 2D objects (std140 padded: each column = vec4)
// ---------------------------------------------------------------------------
struct Std140Mat3 {
    float4 col0;   // x, y, z, pad
    float4 col1;   // x, y, z, pad
    float4 col2;   // x, y, z, pad
};

struct UniformBuffer2D {
    Std140Mat3 model;
    Std140Mat3 view;
    Std140Mat3 proj;
};
[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer2D> ubo;

// ---------------------------------------------------------------------------
// Per-face data SSBO (set 0, binding 1)
// ---------------------------------------------------------------------------
[[vk::binding(1, 0)]] StructuredBuffer<FaceData> faceDataBuffer;

// ---------------------------------------------------------------------------
// Per-vertex position SSBO (set 0, binding 2)
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
    float2 modelPos;
};

struct GSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
    float2 modelPos;
    nointerpolation uint textureId;
    nointerpolation uint effectFlags;
    nointerpolation float params[2];
};

// ---------------------------------------------------------------------------
// Helper: unpack Std140Mat3 to a usable 3×3 matrix multiply
// ---------------------------------------------------------------------------
float3 mulMat3(Std140Mat3 m, float3 v) {
    return float3(
        m.col0.x * v.x + m.col1.x * v.y + m.col2.x * v.z,
        m.col0.y * v.x + m.col1.y * v.y + m.col2.y * v.z,
        m.col0.z * v.x + m.col1.z * v.y + m.col2.z * v.z
    );
}

// ---------------------------------------------------------------------------
// UV coordinates per vertex within a face (2 triangles per quad face)
// ---------------------------------------------------------------------------
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

    // Read 2D position from per-object SSBO
    VertexPosition vp = vertexPositions[vertexID];
    float2 pos = float2(vp.x, vp.y);
    output.modelPos = pos;

    // Transform through model → view → proj (all 3×3)
    // No wave displacement here — that is applied per-face in the
    // geometry shader so that shared vertices between wave and non-wave
    // faces are handled correctly.
    float3 modelPos3 = mulMat3(ubo.model, float3(pos, 1.0));
    float3 viewPos3  = mulMat3(ubo.view,  modelPos3);
    float3 clipPos3  = mulMat3(ubo.proj,  viewPos3);

    // 2D: depth=0 (front), w=1
    output.pos = float4(clipPos3.xy, 0.0, 1.0);

    output.uv = float2(0.0, 0.0);

    output.color = float3(0.8, 0.8, 0.8);

    return output;
}

// ---------------------------------------------------------------------------
// Geometry shader – per-face data lookup and wave displacement
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

    for (int i = 0; i < 3; i++) {
        GSOutput output;

        float2 pos = input[i].modelPos;

        // Apply per-face 2D wave displacement.
        // Non-wave faces (params={0,0}) get zero displacement.
        float2 disp = waveVertexDisplacement2D(pos, pc.time, face.params[0], face.params[1]);
        pos += disp;

        // Re-transform displaced position through model → view → proj
        float3 modelPos3 = mulMat3(ubo.model, float3(pos, 1.0));
        float3 viewPos3  = mulMat3(ubo.view,  modelPos3);
        float3 clipPos3  = mulMat3(ubo.proj,  viewPos3);

        output.pos = float4(clipPos3.xy, 0.0, 1.0);
        output.color        = input[i].color;
        output.modelPos     = pos;
        output.textureId    = face.textureId;
        output.effectFlags  = face.effectFlags;
        output.params[0]    = face.params[0];
        output.params[1]    = face.params[1];

        // UV from draw-order position within each quad
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
            flags, input.uv, input.modelPos, pc.time,
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
