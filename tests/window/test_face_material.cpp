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
// Test: FaceMaterialDesc default values
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
// Test: FaceMaterialDesc with values
// ---------------------------------------------------------------------------
void test_face_material_desc_values() {
  TEST(face_material_desc_values);

  device::FaceMaterialDesc desc;
  desc.textureId = device::TextureId{5};
  desc.effectFlags = device::EFFECT_WAVE | device::EFFECT_DRAWING;
  desc.effectParam0 = 0.1f;
  desc.effectParam1 = 8.0f;

  assert(desc.textureId.index == 5);
  assert(desc.effectFlags == (device::EFFECT_WAVE | device::EFFECT_DRAWING));
  assert(desc.effectParam0 == 0.1f);
  assert(desc.effectParam1 == 8.0f);

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
  auto desc0 = obj.getFaceMaterial(0);
  assert(desc0.effectFlags == 0);

  // Set face material on face 0
  device::FaceMaterialDesc mat;
  mat.textureId = device::TextureId{42};
  mat.effectFlags = device::EFFECT_GRADIENT;
  mat.effectParam0 = 0.5f;
  obj.setFaceMaterial(0, mat);

  auto retrieved = obj.getFaceMaterial(0);
  assert(retrieved.textureId.index == 42);
  assert(retrieved.effectFlags == device::EFFECT_GRADIENT);
  assert(retrieved.effectParam0 == 0.5f);

  // Face 1 should be unchanged
  auto desc1 = obj.getFaceMaterial(1);
  assert(desc1.effectFlags == 0);

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

  device::FaceMaterialDesc mat;
  mat.effectFlags = device::EFFECT_WAVE;
  mat.effectParam0 = 0.05f;
  mat.effectParam1 = 4.0f;
  obj.setFaceMaterial(1, mat);

  auto retrieved = obj.getFaceMaterial(1);
  assert(retrieved.effectFlags == device::EFFECT_WAVE);
  assert(retrieved.effectParam0 == 0.05f);
  assert(retrieved.effectParam1 == 4.0f);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Object face material auto-resize
// ---------------------------------------------------------------------------
void test_face_material_auto_resize() {
  TEST(face_material_auto_resize);

  std::vector<window::Vertex<3>> vertices(3);
  for (auto &v : vertices) {
    v.position = {0.0f, 0.0f, 0.0f};
    v.color = {1.0f, 1.0f, 1.0f};
  }
  std::vector<uint32_t> indices = {0, 1, 2};

  window::Object<3> obj(TEST_OBJ_TAG, std::move(vertices), std::move(indices));

  // Setting face material beyond initial face count should work
  device::FaceMaterialDesc mat;
  mat.effectFlags = device::EFFECT_DRAWING;
  obj.setFaceMaterial(10, mat);

  auto retrieved = obj.getFaceMaterial(10);
  assert(retrieved.effectFlags == device::EFFECT_DRAWING);

  // Out-of-range access returns default
  auto outOfRange = obj.getFaceMaterial(100);
  assert(outOfRange.effectFlags == 0);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Face Material Tests ===\n");

  test_face_material_desc_defaults();
  test_face_material_desc_values();
  test_object3d_face_material();
  test_object2d_face_material();
  test_face_material_auto_resize();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
