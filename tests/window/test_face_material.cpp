#include "object.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

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
// Test: FaceMaterial default values
// ---------------------------------------------------------------------------
void test_face_material_defaults() {
  TEST(face_material_defaults);

  device::FaceMaterial fm{};
  assert(!fm.hasEffects());

  auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == 0);
  assert(gpu.effectParam0 == 0.0f);
  assert(gpu.effectParam1 == 0.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: FaceMaterial with values
// ---------------------------------------------------------------------------
void test_face_material_values() {
  TEST(face_material_values);

  device::FaceMaterial fm;
  fm.textureId = 5;
  fm.addEffect(device::FaceEffect{device::EffectType::eWave, 0.1f, 8.0f});
  fm.addEffect(device::FaceEffect{device::EffectType::eDrawing});

  assert(fm.textureId == 5);

  auto gpu = device::GPUFaceData::fromFaceMaterial(fm);
  assert(gpu.effectFlags == (0x02u | 0x04u)); // WAVE | DRAWING
  assert(gpu.effectParam0 == 0.1f);
  assert(gpu.effectParam1 == 8.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Helper tags for testing
// ---------------------------------------------------------------------------
static constexpr device::ShaderTag TEST_SHADER_TAG{
    "test_shader", "test.slang", "vertMain", "fragMain"};
static constexpr window::MaterialTag TEST_MATERIAL_TAG{
    "test_material", &TEST_SHADER_TAG};
static constexpr window::ObjectTag TEST_OBJ_TAG{
    "test_obj", &TEST_MATERIAL_TAG};

// ---------------------------------------------------------------------------
// Test: Object<3> face material set/get
// ---------------------------------------------------------------------------
void test_object3d_face_material() {
  TEST(object3d_face_material);

  // Create a simple 2-triangle object (6 vertices)
  std::vector<window::Vertex<3>> vertices(6);
  for (auto &v : vertices) {
    v.position = {0.0f, 0.0f, 0.0f};
    v.color = {1.0f, 1.0f, 1.0f};
  }
  std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};

  window::Object<3> obj(TEST_OBJ_TAG, std::move(vertices), std::move(indices));

  // Default face materials
  auto fm0 = obj.getFaceMaterial(0);
  auto gpu0 = device::GPUFaceData::fromFaceMaterial(fm0);
  assert(gpu0.effectFlags == 0);

  // Set face material on face 0
  device::FaceMaterial mat;
  mat.textureId = 42;
  mat.addEffect(device::FaceEffect{device::EffectType::eGradient, 0.5f, 0.0f});
  obj.setFaceMaterial(0, mat);

  auto retrieved = obj.getFaceMaterial(0);
  assert(retrieved.textureId == 42);
  auto gpuR = device::GPUFaceData::fromFaceMaterial(retrieved);
  assert(gpuR.effectFlags == 0x01u); // GRADIENT
  assert(gpuR.effectParam0 == 0.5f);

  // Face 1 should be unchanged
  auto fm1 = obj.getFaceMaterial(1);
  auto gpu1 = device::GPUFaceData::fromFaceMaterial(fm1);
  assert(gpu1.effectFlags == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Object<2> face material set/get
// ---------------------------------------------------------------------------
void test_object2d_face_material() {
  TEST(object2d_face_material);

  std::vector<window::Vertex<2>> vertices(6);
  for (auto &v : vertices) {
    v.position = {0.0f, 0.0f};
    v.color = {1.0f, 1.0f, 1.0f};
  }
  std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};

  window::Object<2> obj(TEST_OBJ_TAG, std::move(vertices), std::move(indices));

  device::FaceMaterial mat;
  mat.addEffect(device::FaceEffect{device::EffectType::eWave, 0.05f, 4.0f});
  obj.setFaceMaterial(1, mat);

  auto retrieved = obj.getFaceMaterial(1);
  auto gpuR = device::GPUFaceData::fromFaceMaterial(retrieved);
  assert(gpuR.effectFlags == 0x02u); // WAVE
  assert(gpuR.effectParam0 == 0.05f);
  assert(gpuR.effectParam1 == 4.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Object face index validation (out-of-range ignored)
// ---------------------------------------------------------------------------
void test_face_material_index_validation() {
  TEST(face_material_index_validation);

  std::vector<window::Vertex<3>> vertices(3);
  for (auto &v : vertices) {
    v.position = {0.0f, 0.0f, 0.0f};
    v.color = {1.0f, 1.0f, 1.0f};
  }
  std::vector<uint32_t> indices = {0, 1, 2};

  window::Object<3> obj(TEST_OBJ_TAG, std::move(vertices), std::move(indices));

  // Object has 1 face (indices 0,1,2). Setting face 10 should be rejected
  // (logged as error and ignored since faces_ is non-empty and 10 >= 1).
  device::FaceMaterial mat;
  mat.addEffect(device::FaceEffect{device::EffectType::eDrawing});
  obj.setFaceMaterial(10, mat);

  // Out-of-range read returns default
  auto outOfRange = obj.getFaceMaterial(10);
  auto gpuOOR = device::GPUFaceData::fromFaceMaterial(outOfRange);
  assert(gpuOOR.effectFlags == 0);

  // Valid face 0 read returns default (was never set)
  auto valid = obj.getFaceMaterial(0);
  auto gpuV = device::GPUFaceData::fromFaceMaterial(valid);
  assert(gpuV.effectFlags == 0);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Face Material Tests ===\n");

  test_face_material_defaults();
  test_face_material_values();
  test_object3d_face_material();
  test_object2d_face_material();
  test_face_material_index_validation();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
