// 2D Unified quad shader - renders a multi-face 2D object with per-face
// rendering modes and geometry shader for GPU-side processing.
//
// Uses 3x3 matrices for 2D homogeneous coordinate transforms.
// For 2D objects: UBO contains 3x3 matrices (model, view, proj)
//
// Rendering modes (similar to cube faces):
//   Mode 0: Plain uniform color
//   Mode 1: Checkerboard texture (sampled from bound texture slot 0)
//   Mode 2: Vertical gradient
//   Mode 3: Animated wave function

struct UniformBuffer {
    float3x3 model;
    float3x3 view;
    float3x3 proj;
};
ConstantBuffer<UniformBuffer> ubo;

struct PushData {
    float time;
};
[[vk::push_constant]] ConstantBuffer<PushData> pc;

// Texture sampler bound from Object's per-face texture assignment
// Slot 0: checkerboard texture (checkerboard.png)
[[vk::binding(1, 0)]] Sampler2D checkerboardTex;

struct VSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float2 modelPos;
};

struct GSOutput {
    float4 pos : SV_Position;
    nointerpolation uint renderMode;
    float3 color;
    float2 uv;
    float2 modelPos;
};

// Quad made of 4 triangular faces (8 triangles total for a 2x2 grid)
// Each quadrant has a different render mode
static const float2 quadPositions[16] = {
    // Face 0 (top-left quadrant): plain color - triangle 0
    float2(-0.8,  0.0), float2( 0.0,  0.0), float2( 0.0,  0.8),
    // Face 0: triangle 1
    float2(-0.8,  0.0), float2( 0.0,  0.8), float2(-0.8,  0.8),
    // Face 1 (top-right quadrant): checkerboard texture - triangle 2
    float2( 0.0,  0.0), float2( 0.8,  0.0), float2( 0.8,  0.8),
    // Face 1: triangle 3
    float2( 0.0,  0.0), float2( 0.8,  0.8), float2( 0.0,  0.8),
    // Face 2 (bottom-left quadrant): gradient - triangle 4
    float2(-0.8, -0.8), float2( 0.0, -0.8), float2( 0.0,  0.0),
    // Face 2: triangle 5
    float2(-0.8, -0.8), float2( 0.0,  0.0), float2(-0.8,  0.0),
    // Face 3 (bottom-right quadrant): wave - not stored, generated inline
};

// Per-triangle render mode (8 triangles = 4 faces × 2 triangles each)
static const uint triRenderMode[8] = {
    0, 0,  // Face 0: plain color
    1, 1,  // Face 1: checkerboard texture
    2, 2,  // Face 2: gradient
    3, 3,  // Face 3: wave
};

// UV coordinates per vertex within a face quad
static const float2 quadUVs[6] = {
    float2(0.0, 1.0),
    float2(1.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 0.0),
    float2(0.0, 0.0),
};

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    uint triIndex = vertexID / 3;
    uint localVert = vertexID % 3;

    float2 pos;
    if (triIndex < 6) {
        // First 6 triangles use quadPositions lookup
        // But we have 16 positions laid out as 8 groups of 3
        // (though the array is 16 entries, we use indices differently)
        uint baseIdx = triIndex * 3 + localVert;
        if (baseIdx < 18) {
            pos = quadPositions[min(baseIdx, 15u)];
        } else {
            pos = float2(0.0, 0.0);
        }
    }

    // Face 3 (bottom-right quadrant): wave - procedurally generated
    if (triIndex == 6) {
        float2 facePositions[3] = {
            float2( 0.0, -0.8), float2( 0.8, -0.8), float2( 0.8,  0.0)
        };
        pos = facePositions[localVert];
    } else if (triIndex == 7) {
        float2 facePositions[3] = {
            float2( 0.0, -0.8), float2( 0.8,  0.0), float2( 0.0,  0.0)
        };
        pos = facePositions[localVert];
    }

    output.modelPos = pos;
    output.renderMode = (triIndex < 8) ? triRenderMode[triIndex] : 0u;

    // UV from local vertex in face
    uint uvIdx = vertexID % 6;
    output.uv = quadUVs[uvIdx];

    // 2D homogeneous coordinates: (x, y, 1)
    float3 pos2d = float3(pos, 1.0);
    float3 worldPos = mul(ubo.model, pos2d);
    float3 viewPos = mul(ubo.view, worldPos);
    float3 clipPos = mul(ubo.proj, viewPos);

    // Convert 2D result to clip space float4
    output.pos = float4(clipPos.xy, 0.0, 1.0);
    output.color = float3(0.4, 0.6, 0.9); // Default: light blue

    return output;
}

// ---------------------------------------------------------------------------
// Geometry shader - processes triangles with edge highlighting on GPU
// ---------------------------------------------------------------------------
[shader("geometry")]
[maxvertexcount(3)]
void geomMain(
    triangle VSOutput input[3],
    inout TriangleStream<GSOutput> outputStream
) {
    // Compute triangle area on GPU for potential effects
    float2 e1 = input[1].modelPos - input[0].modelPos;
    float2 e2 = input[2].modelPos - input[0].modelPos;
    float area = abs(e1.x * e2.y - e1.y * e2.x) * 0.5;

    // Brightness based on area (larger faces slightly dimmer)
    float areaFactor = clamp(1.0 - area * 0.3, 0.7, 1.0);

    for (int i = 0; i < 3; i++) {
        GSOutput output;
        output.pos = input[i].pos;
        output.renderMode = input[i].renderMode;
        output.color = input[i].color * areaFactor;
        output.uv = input[i].uv;
        output.modelPos = input[i].modelPos;
        outputStream.Append(output);
    }
    outputStream.RestartStrip();
}

// ---------------------------------------------------------------------------
// Fragment mode implementations
// ---------------------------------------------------------------------------

// Mode 0: Plain color
float4 modePlainColor(GSOutput input) {
    return float4(input.color, 1.0);
}

// Mode 1: Checkerboard texture from bound texture slot 0
float4 modeCheckerboard(GSOutput input) {
    float4 texColor = checkerboardTex.Sample(input.uv);
    return texColor;
}

// Mode 2: Vertical gradient
float4 modeGradient(GSOutput input) {
    float t = (input.modelPos.y + 0.8) / 1.6; // Normalize to [0, 1]

    float3 bottomColor = float3(0.1, 0.0, 0.3);
    float3 topColor = float3(0.9, 0.5, 0.1);
    float3 color = lerp(bottomColor, topColor, t);

    return float4(color, 1.0);
}

// Mode 3: Animated wave function
float4 modeWave(GSOutput input) {
    float2 uv = input.uv;
    float t = pc.time;

    float wave1 = sin(uv.x * 10.0 + t * 3.0) * 0.5 + 0.5;
    float wave2 = cos(uv.y * 8.0 + t * 2.0) * 0.5 + 0.5;
    float wave3 = sin((uv.x + uv.y) * 6.0 + t * 1.5) * 0.5 + 0.5;

    float3 color;
    color.r = wave1 * 0.7 + 0.3;
    color.g = wave2 * 0.5 + 0.2;
    color.b = wave3 * 0.8 + 0.2;

    return float4(saturate(color), 1.0);
}

// Unified fragment shader: selects mode per-triangle
[shader("fragment")]
float4 fragMain(GSOutput input) : SV_TARGET {
    switch (input.renderMode) {
        case 1:  return modeCheckerboard(input);
        case 2:  return modeGradient(input);
        case 3:  return modeWave(input);
        default: return modePlainColor(input);
    }
}
