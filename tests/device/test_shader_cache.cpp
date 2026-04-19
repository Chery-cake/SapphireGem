#include "bindless_types.h"
#include "shader_manager.h"
#include "texture_table.h"
#include <cassert>
#include <cstdio>
#include <string>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                             \
  do {                                                                         \
    tests_run++;                                                               \
    std::printf("  [TEST] %s ... ", #name);                                    \
  } while (0)

#define PASS()                                                                 \
  do {                                                                         \
    tests_passed++;                                                            \
    std::printf("PASSED\n");                                                   \
  } while (0)

// ---------------------------------------------------------------------------
// Test: ShaderTag construction
// ---------------------------------------------------------------------------
void test_shader_tag_construction() {
  TEST(shader_tag_construction);

  static constexpr device::ShaderTag tag{"test_shader", "test.slang",
                                         "vertMain", "fragMain", "geomMain"};
  assert(std::string(tag.name) == "test_shader");
  assert(std::string(tag.sourcePath) == "test.slang");
  assert(std::string(tag.vertexEntry) == "vertMain");
  assert(std::string(tag.fragmentEntry) == "fragMain");
  assert(std::string(tag.geometryEntry) == "geomMain");
  assert(tag.computeEntry == nullptr);
  assert(tag.tessCtrlEntry == nullptr);
  assert(tag.tessEvalEntry == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ShaderManager default state (not initialized)
// ---------------------------------------------------------------------------
void test_shader_manager_default() {
  TEST(shader_manager_default);

  device::ShaderManager mgr;
  assert(!mgr.isInitialized());

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ShaderError struct
// ---------------------------------------------------------------------------
void test_shader_error_struct() {
  TEST(shader_error_struct);

  device::ShaderError err{"test error message"};
  assert(err.message == "test error message");

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ShaderStage enum values
// ---------------------------------------------------------------------------
void test_shader_stage_values() {
  TEST(shader_stage_values);

  assert(static_cast<uint8_t>(device::ShaderStage::Vertex) == 0);
  assert(static_cast<uint8_t>(device::ShaderStage::Fragment) == 1);
  assert(static_cast<uint8_t>(device::ShaderStage::Geometry) == 2);
  assert(static_cast<uint8_t>(device::ShaderStage::Compute) == 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUFaceData layout
// ---------------------------------------------------------------------------
void test_gpu_face_data_layout() {
  TEST(gpu_face_data_layout);

  // GPUFaceData must be 16 bytes for std430 layout
  static_assert(sizeof(device::GPUFaceData) == 16,
                "GPUFaceData must be 16 bytes");

  // Build a FaceMaterial with wave + gradient, then convert
  device::FaceMaterial fm{};
  (void)fm.addEffect(
      device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
  (void)fm.addEffect(
      device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
  device::GPUFaceData data = device::GPUFaceData::fromFaceMaterial(fm);

  assert(data.textureId == device::TextureId::INVALID);
  assert(data.effectFlags == (0x02u | 0x01u)); // WAVE | GRADIENT
  assert(data.params[0] == 0.05f);
  assert(data.params[1] == 4.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Effect flag constants (GPU-side bitmask values)
// ---------------------------------------------------------------------------
void test_effect_flag_constants() {
  TEST(effect_flag_constants);

  // The GPU-side bitmask values are derived from EffectType enum:
  //   eGradient -> 0x01, eWave -> 0x02, eDrawing -> 0x04
  // Verify via GPUFaceData::fromFaceMaterial

  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
    auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
    assert(gpu.effectFlags == 0x01u);
  }
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.0f, 0.0f});
    auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
    assert(gpu.effectFlags == 0x02u);
  }
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(device::FaceEffect{device::EffectType::eDrawing});
    auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
    assert(gpu.effectFlags == 0x04u);
  }

  // No overlap
  assert((0x01u & 0x02u) == 0);
  assert((0x01u & 0x04u) == 0);
  assert((0x02u & 0x04u) == 0);

  // Combining
  {
    device::FaceMaterial fm;
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f});
    (void)fm.addEffect(
        device::FaceEffect{device::EffectType::eWave, 0.0f, 0.0f});
    auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
    assert((gpu.effectFlags & 0x01u) != 0); // gradient set
    assert((gpu.effectFlags & 0x02u) != 0); // wave set
    assert((gpu.effectFlags & 0x04u) == 0); // drawing not set
  }

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceMaterial defaults
// ---------------------------------------------------------------------------
void test_face_material_defaults() {
  TEST(face_material_defaults);

  device::FaceMaterial fm{};
  assert(!fm.hasEffects());

  // Convert to GPU data and verify
  auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == 0);
  assert(gpu.params[0] == 0.0f);
  assert(gpu.params[1] == 0.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceMaterial effects array operations
// ---------------------------------------------------------------------------
void test_face_material_effects_array() {
  TEST(face_material_effects_array);

  device::FaceMaterial fm{};

  // Add a gradient effect
  assert(fm.addEffect(
      device::FaceEffect{device::EffectType::eGradient, 0.5f, 1.0f}));
  assert(fm.hasEffects());

  auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == 0x01u); // GRADIENT
  assert(gpu.params[0] == 0.5f);
  assert(gpu.params[1] == 1.0f);

  // Add a wave effect
  assert(
      fm.addEffect(device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f}));
  gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == (0x01u | 0x02u)); // GRADIENT | WAVE
  // First effect's params are kept
  assert(gpu.params[0] == 0.5f);
  assert(gpu.params[1] == 1.0f);

  // Remove gradient
  assert(fm.removeEffect(device::EffectType::eGradient));
  gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == 0x02u); // WAVE only
  // Now wave is the first active effect
  assert(gpu.params[0] == 0.05f);
  assert(gpu.params[1] == 4.0f);

  // Clear all
  fm.clearEffects();
  assert(!fm.hasEffects());
  gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceMaterial max effects limit
// ---------------------------------------------------------------------------
void test_face_material_max_effects() {
  TEST(face_material_max_effects);

  device::FaceMaterial fm{};

  // Fill all slots
  for (uint32_t i = 0; i < device::MAX_FACE_EFFECTS; ++i) {
    assert(fm.addEffect(
        device::FaceEffect{device::EffectType::eGradient, 0.0f, 0.0f}));
  }

  // Next add should fail
  assert(
      !fm.addEffect(device::FaceEffect{device::EffectType::eWave, 0.0f, 0.0f}));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUFaceData::fromFaceMaterial
// ---------------------------------------------------------------------------
void test_gpu_face_data_from_face_material() {
  TEST(gpu_face_data_from_face_material);

  device::FaceMaterial fm{};
  fm.textureId = 42;
  (void)fm.addEffect(device::FaceEffect{device::EffectType::eWave, 0.1f, 8.0f});
  (void)fm.addEffect(device::FaceEffect{device::EffectType::eDrawing});

  device::GPUFaceData gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.textureId == 42);
  assert(gpu.effectFlags == (0x02u | 0x04u)); // WAVE | DRAWING
  assert(gpu.params[0] == 0.1f);
  assert(gpu.params[1] == 8.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: BindlessPushConstants layout
// ---------------------------------------------------------------------------
void test_bindless_push_constants() {
  TEST(bindless_push_constants);

  device::BindlessPushConstants pc{};
  assert(pc.time == 0.0f);
  assert(pc.objectId == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: EffectType enum values
// ---------------------------------------------------------------------------
void test_effect_type_enum() {
  TEST(effect_type_enum);

  assert(static_cast<uint32_t>(device::EffectType::eNone) == 0);
  assert(static_cast<uint32_t>(device::EffectType::eGradient) == 1);
  assert(static_cast<uint32_t>(device::EffectType::eWave) == 2);
  assert(static_cast<uint32_t>(device::EffectType::eDrawing) == 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceEffectType enum values
// ---------------------------------------------------------------------------
void test_face_effect_type_enum() {
  TEST(face_effect_type_enum);

  assert(static_cast<uint32_t>(device::FaceEffectType::eNone) == 0);
  assert(static_cast<uint32_t>(device::FaceEffectType::eWave) == 1);
  assert(static_cast<uint32_t>(device::FaceEffectType::eRipple) == 2);
  assert(static_cast<uint32_t>(device::FaceEffectType::eCount) == 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceEffectId handle
// ---------------------------------------------------------------------------
void test_face_effect_id() {
  TEST(face_effect_id);

  device::FaceEffectId defaultId;
  assert(!defaultId.isValid());
  assert(defaultId.index == device::FaceEffectId::INVALID);

  device::FaceEffectId validId;
  validId.index = 42;
  assert(validId.isValid());
  assert(validId != defaultId);

  device::FaceEffectId sameId;
  sameId.index = 42;
  assert(validId == sameId);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceEffectEntry construction
// ---------------------------------------------------------------------------
void test_face_effect_entry() {
  TEST(face_effect_entry);

  // Default construction
  device::FaceEffectEntry defaultFx;
  assert(!defaultFx.isActive());
  assert(defaultFx.effect == device::FaceEffectType::eNone);
  assert(defaultFx.paramCount == 0);
  assert(defaultFx.params.empty());

  // Construct with type and count
  device::FaceEffectEntry waveFx(device::FaceEffectType::eWave, 4);
  assert(waveFx.isActive());
  assert(waveFx.effect == device::FaceEffectType::eWave);
  assert(waveFx.paramCount == 4);
  assert(waveFx.params.size() == 4);
  assert(waveFx.params[0] == 0.0f);

  // Construct with explicit params
  device::FaceEffectEntry rippleFx(device::FaceEffectType::eRipple,
                                   {0.1f, 2.0f, 0.0f, 1.0f, 0.5f, 0.5f});
  assert(rippleFx.isActive());
  assert(rippleFx.paramCount == 6);
  assert(rippleFx.params.size() == 6);
  assert(rippleFx.params[0] == 0.1f);
  assert(rippleFx.params[4] == 0.5f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUFaceEffectRecord layout
// ---------------------------------------------------------------------------
void test_gpu_face_effect_record_layout() {
  TEST(gpu_face_effect_record_layout);

  static_assert(sizeof(device::GPUFaceEffectRecord) == 16,
                "GPUFaceEffectRecord must be 16 bytes");

  device::GPUFaceEffectRecord rec{};
  assert(rec.effectType == 0);
  assert(rec.paramCount == 0);
  assert(rec.firstParam == 0);

  rec.effectType = static_cast<uint32_t>(device::FaceEffectType::eWave);
  rec.paramCount = 4;
  rec.firstParam = 10;
  assert(rec.effectType == 1);
  assert(rec.paramCount == 4);
  assert(rec.firstParam == 10);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GPUFaceEffectParam layout
// ---------------------------------------------------------------------------
void test_gpu_face_effect_param_layout() {
  TEST(gpu_face_effect_param_layout);

  static_assert(sizeof(device::GPUFaceEffectParam) == 16,
                "GPUFaceEffectParam must be 16 bytes");

  device::GPUFaceEffectParam p{};
  assert(p.value == 0.0f);

  p.value = 3.14f;
  assert(p.value == 3.14f);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Shader Cache & GPU Data Tests ===\n");

  test_shader_tag_construction();
  test_shader_manager_default();
  test_shader_error_struct();
  test_shader_stage_values();
  test_gpu_face_data_layout();
  test_effect_flag_constants();
  test_face_material_defaults();
  test_face_material_effects_array();
  test_face_material_max_effects();
  test_gpu_face_data_from_face_material();
  test_bindless_push_constants();
  test_effect_type_enum();
  test_face_effect_type_enum();
  test_face_effect_id();
  test_face_effect_entry();
  test_gpu_face_effect_record_layout();
  test_gpu_face_effect_param_layout();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
