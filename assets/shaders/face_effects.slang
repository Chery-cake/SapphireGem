// face_effects.slang
// Per-face rendering effect implementations dispatched via effectFlags bitmask.
//
// Effect flag bits (must match device::EFFECT_* constants in bindless_types.h):
//   bit 0 (0x01): EFFECT_GRADIENT  – vertical gradient colour
//   bit 1 (0x02): EFFECT_WAVE      – wave UV displacement / colour
//   bit 2 (0x04): EFFECT_DRAWING   – procedural drawing pattern

import bindless_common;

// ---------------------------------------------------------------------------
// Per-face GPU data SSBO struct (must match device::GPUFaceData exactly)
// ---------------------------------------------------------------------------
struct FaceData {
    uint  textureId;     // Index into TextureRecord[]
    uint  effectFlags;   // Bitmask
    float effectParam0;  // e.g. wave amplitude
    float effectParam1;  // e.g. wave frequency
};

// Effect flag constants
static const uint EFFECT_GRADIENT = 0x01;
static const uint EFFECT_WAVE     = 0x02;
static const uint EFFECT_DRAWING  = 0x04;

// ---------------------------------------------------------------------------
// Gradient effect: vertical gradient from bottom to top colour
// ---------------------------------------------------------------------------
inline float4 applyGradient(float2 modelPos, float yRange) {
    float t = (modelPos.y + yRange * 0.5) / max(yRange, 0.001);
    t = saturate(t);

    float3 bottomColor = float3(0.3, 0.0, 0.5);
    float3 topColor = float3(0.0, 0.8, 0.9);
    float3 color = lerp(bottomColor, topColor, t);
    return float4(color, 1.0);
}

// ---------------------------------------------------------------------------
// Wave effect: sine-wave colour visualization
// ---------------------------------------------------------------------------
inline float4 applyWaveColor(float2 uv, float time, float amplitude,
                             float frequency) {
    float amp = amplitude > 0.0 ? amplitude : 0.05;
    float freq = frequency > 0.0 ? frequency : 4.0;

    float wave1 = sin(uv.x * freq * 2.5 + time * 3.0) * 0.5 + 0.5;
    float wave2 = cos(uv.y * freq * 2.0 + time * 2.0) * 0.5 + 0.5;
    float wave3 = sin((uv.x + uv.y) * freq * 1.5 + time * 1.5) * 0.5 + 0.5;

    float3 color;
    color.r = wave1 * 0.7 + 0.3;
    color.g = wave2 * 0.5 + 0.2;
    color.b = wave3 * 0.8 + 0.2;

    return float4(saturate(color), 1.0);
}

// ---------------------------------------------------------------------------
// Wave vertex displacement (applied in vertex shader)
// ---------------------------------------------------------------------------
inline float3 waveVertexDisplacement(float3 pos, float time, float amplitude,
                                     float frequency) {
    float amp = amplitude > 0.0 ? amplitude : 0.05;
    float freq = frequency > 0.0 ? frequency : 4.0;

    float wave = sin(pos.x * freq + time * 2.0) * amp;
    wave += cos(pos.z * freq + time * 1.5) * amp;
    return float3(0.0, wave, 0.0);
}

// ---------------------------------------------------------------------------
// 2D wave vertex displacement (for Object<2>)
// ---------------------------------------------------------------------------
inline float2 waveVertexDisplacement2D(float2 pos, float time, float amplitude,
                                       float frequency) {
    float amp = amplitude > 0.0 ? amplitude : 0.04;
    float freq = frequency > 0.0 ? frequency : 6.0;

    float dx = sin(pos.y * freq + time * 2.5) * amp;
    float dy = cos(pos.x * freq * 0.83 + time * 2.0) * amp;
    return float2(dx, dy);
}

// ---------------------------------------------------------------------------
// Procedural drawing effect: concentric rings SDF pattern
// ---------------------------------------------------------------------------
inline float4 applyDrawing(float2 uv, float time) {
    float2 center = float2(0.5, 0.5);
    float dist = length(uv - center);

    // Animated concentric rings
    float rings = sin(dist * 30.0 - time * 3.0) * 0.5 + 0.5;
    // Cross-hatch Lissajous overlay
    float lissX = sin(uv.x * 20.0 + time * 2.0) * 0.5 + 0.5;
    float lissY = cos(uv.y * 15.0 + time * 1.5) * 0.5 + 0.5;
    float pattern = rings * 0.7 + (lissX * lissY) * 0.3;

    float3 color = float3(pattern * 0.2, pattern * 0.6, pattern * 0.9);
    return float4(saturate(color), 1.0);
}

// ---------------------------------------------------------------------------
// Dispatch per-face effect based on effectFlags bitmask
// Returns the modified colour. If multiple effects are set, they are blended.
// ---------------------------------------------------------------------------
inline float4 dispatchFaceEffect(uint effectFlags, float2 uv, float2 modelPos,
                                 float time, float amplitude, float frequency,
                                 float yRange) {
    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    int count = 0;

    if ((effectFlags & EFFECT_GRADIENT) != 0) {
        result += applyGradient(modelPos, yRange);
        count++;
    }
    if ((effectFlags & EFFECT_WAVE) != 0) {
        result += applyWaveColor(uv, time, amplitude, frequency);
        count++;
    }
    if ((effectFlags & EFFECT_DRAWING) != 0) {
        result += applyDrawing(uv, time);
        count++;
    }

    if (count > 1) {
        result /= float(count);
    }

    return result;
}
