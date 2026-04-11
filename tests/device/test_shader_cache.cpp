#include "bindless_types.h"
#include "shader_manager.h"
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

  device::GPUFaceData data{};
  data.textureId = 42;
  data.effectFlags = device::EFFECT_WAVE | device::EFFECT_GRADIENT;
  data.effectParam0 = 0.05f;
  data.effectParam1 = 4.0f;

  assert(data.textureId == 42);
  assert(data.effectFlags == (device::EFFECT_WAVE | device::EFFECT_GRADIENT));
  assert(data.effectParam0 == 0.05f);
  assert(data.effectParam1 == 4.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Effect flag constants
// ---------------------------------------------------------------------------
void test_effect_flag_constants() {
  TEST(effect_flag_constants);

  assert(device::EFFECT_NONE == 0);
  assert(device::EFFECT_GRADIENT == (1u << 0));
  assert(device::EFFECT_WAVE == (1u << 1));
  assert(device::EFFECT_DRAWING == (1u << 2));

  // No overlap
  assert((device::EFFECT_GRADIENT & device::EFFECT_WAVE) == 0);
  assert((device::EFFECT_GRADIENT & device::EFFECT_DRAWING) == 0);
  assert((device::EFFECT_WAVE & device::EFFECT_DRAWING) == 0);

  // Combining
  uint32_t combined = device::EFFECT_GRADIENT | device::EFFECT_WAVE;
  assert((combined & device::EFFECT_GRADIENT) != 0);
  assert((combined & device::EFFECT_WAVE) != 0);
  assert((combined & device::EFFECT_DRAWING) == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceMaterialDesc defaults
// ---------------------------------------------------------------------------
void test_face_material_desc_defaults() {
  TEST(face_material_desc_defaults);

  device::FaceMaterialDesc desc{};
  assert(desc.effectFlags == 0);
  assert(desc.effectParam0 == 0.0f);
  assert(desc.effectParam1 == 0.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: BindlessPushConstants layout
// ---------------------------------------------------------------------------
void test_bindless_push_constants() {
  TEST(bindless_push_constants);

  device::BindlessPushConstants pc{};
  assert(pc.time == 0.0f);
  assert(pc.textureId == device::TextureId::INVALID);
  assert(pc.atlasTextureId == device::TextureId::INVALID);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ComputedGeometryBuffer default state
// ---------------------------------------------------------------------------
void test_computed_geometry_buffer_default() {
  TEST(computed_geometry_buffer_default);

  device::ComputedGeometryBuffer buf;
  assert(buf.vertexCount == 0);
  assert(buf.faceCount == 0);
  assert(!buf.precomputed);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ComputeRenderer default state
// ---------------------------------------------------------------------------
void test_compute_renderer_default() {
  TEST(compute_renderer_default);

  device::ComputeRenderer renderer;
  assert(!renderer.isInitialized());
  assert(renderer.getBuffer("nonexistent") == nullptr);

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
  test_face_material_desc_defaults();
  test_bindless_push_constants();
  test_computed_geometry_buffer_default();
  test_compute_renderer_default();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
