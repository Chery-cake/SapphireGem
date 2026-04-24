#include "resource_registry.h"
#include <cassert>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
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

// --- Test tag and asset types ---

struct TestTag {
  const char *name;
  const int value;

  constexpr TestTag(const char *n, int v) : name(n), value(v) {}
};

class TestAsset {
public:
  explicit TestAsset(const TestTag &tag) : name_(tag.name), value_(tag.value) {}
  TestAsset(const TestTag &tag, int extra)
      : name_(tag.name), value_(tag.value + extra) {}

  const std::string &name() const { return name_; }
  int value() const { return value_; }

private:
  std::string name_;
  int value_;
};

// Static tag instances (required for pointer stability)
static constexpr TestTag TAG_A{"alpha", 1};
static constexpr TestTag TAG_B{"beta", 2};
static constexpr TestTag TAG_C{"gamma", 3};
static constexpr TestTag TAG_D{"delta", 4};

// ---------------------------------------------------------------------------
// Test: Empty registry
// ---------------------------------------------------------------------------
void test_empty_registry() {
  TEST(empty_registry);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  assert(reg.empty());
  assert(reg.size() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Add and get
// ---------------------------------------------------------------------------
void test_add_and_get() {
  TEST(add_and_get);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  bool added = reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(added);
  assert(reg.size() == 1);

  auto *asset = reg.get(&TAG_A);
  assert(asset != nullptr);
  assert(asset->name() == "alpha");
  assert(asset->value() == 1);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Duplicate add returns false
// ---------------------------------------------------------------------------
void test_duplicate_add() {
  TEST(duplicate_add);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  bool duplicate = reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(!duplicate);
  assert(reg.size() == 1);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Contains
// ---------------------------------------------------------------------------
void test_contains() {
  TEST(contains);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));

  assert(reg.contains(&TAG_A));
  assert(!reg.contains(&TAG_B));

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Remove
// ---------------------------------------------------------------------------
void test_remove() {
  TEST(remove);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));

  bool removed = reg.remove(&TAG_A);
  assert(removed);
  assert(!reg.contains(&TAG_A));
  assert(reg.empty());

  // Remove nonexistent
  bool notFound = reg.remove(&TAG_B);
  assert(!notFound);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Emplace
// ---------------------------------------------------------------------------
void test_emplace() {
  TEST(emplace);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  bool emplaced = reg.emplace(&TAG_B);
  assert(emplaced);

  auto *asset = reg.get(&TAG_B);
  assert(asset != nullptr);
  assert(asset->name() == "beta");
  assert(asset->value() == 2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Emplace with extra args
// ---------------------------------------------------------------------------
void test_emplace_extra_args() {
  TEST(emplace_extra_args);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  reg.emplace(&TAG_C, 100);
  auto *asset = reg.get(&TAG_C);
  assert(asset != nullptr);
  assert(asset->value() == 103); // gamma=3 + 100

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Set (replace)
// ---------------------------------------------------------------------------
void test_set() {
  TEST(set);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  bool wasNew = reg.set(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(wasNew);

  // Replace with different value by creating a new asset with extra
  // constructor
  bool wasReplace = reg.set(&TAG_A, std::make_unique<TestAsset>(TAG_A, 100));
  assert(!wasReplace); // false = replaced existing

  auto *asset = reg.get(&TAG_A);
  assert(asset->value() == 101); // alpha=1 + 100

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get entry
// ---------------------------------------------------------------------------
void test_get_entry() {
  TEST(get_entry);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));

  auto entry = reg.getEntry(&TAG_A);
  assert(entry.tag == &TAG_A);
  assert(entry.asset != nullptr);
  assert(entry.asset->name() == "alpha");

  auto missing = reg.getEntry(&TAG_B);
  assert(missing.tag == nullptr);
  assert(missing.asset == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Extract
// ---------------------------------------------------------------------------
void test_extract() {
  TEST(extract);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));

  auto extracted = reg.extract(&TAG_A);
  assert(extracted != nullptr);
  assert(extracted->name() == "alpha");
  assert(reg.empty());

  // Extract nonexistent
  auto missing = reg.extract(&TAG_B);
  assert(missing == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ForEach
// ---------------------------------------------------------------------------
void test_for_each() {
  TEST(for_each);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));

  int count = 0;
  reg.forEach([&count](const TestTag *, TestAsset *) { count++; });
  assert(count == 2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: GetAll
// ---------------------------------------------------------------------------
void test_get_all() {
  TEST(get_all);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));
  reg.add(&TAG_C, std::make_unique<TestAsset>(TAG_C));

  auto all = reg.getAll();
  assert(all.size() == 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Clear
// ---------------------------------------------------------------------------
void test_clear() {
  TEST(clear);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));

  reg.clear();
  assert(reg.empty());
  assert(reg.size() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Add callback
// ---------------------------------------------------------------------------
void test_add_callback() {
  TEST(add_callback);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  bool callbackFired = false;
  const TestTag *receivedTag = nullptr;
  TestAsset *testAsset = nullptr;

  auto res = reg.onAdd([&callbackFired, &receivedTag,
                        &testAsset](const TestTag *tag, TestAsset *asset) {
    callbackFired = true;
    receivedTag = tag;
    testAsset = asset;
  });

  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(callbackFired);
  assert(receivedTag == &TAG_A);

  assert(testAsset != nullptr);
  assert(testAsset->name() == "alpha");
  assert(testAsset->value() == 1);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Remove callback
// ---------------------------------------------------------------------------
void test_remove_callback() {
  TEST(remove_callback);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  bool callbackFired = false;
  const TestTag *receivedTag = nullptr;

  auto res = reg.onRemove(
      [&callbackFired, &receivedTag](const TestTag *tag, TestAsset *asset) {
        callbackFired = true;
        receivedTag = tag;

        assert(asset->name() == "alpha");
        assert(asset->value() == 1);
      });

  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.remove(&TAG_A);
  assert(callbackFired);
  assert(receivedTag == &TAG_A);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Clear callbacks
// ---------------------------------------------------------------------------
void test_clear_callbacks() {
  TEST(clear_callbacks);

  core::ResourceRegistry<TestTag, TestAsset> reg;

  int addCount = 0;
  reg.onAdd([&addCount](const TestTag *, TestAsset *) { addCount++; });
  reg.onRemove([&addCount](const TestTag *, TestAsset *) { addCount++; });

  reg.clearCallbacks();

  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.remove(&TAG_A);
  assert(addCount == 0); // Callback was cleared

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Get returns nullptr for missing tag
// ---------------------------------------------------------------------------
void test_get_missing() {
  TEST(get_missing);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  auto *asset = reg.get(&TAG_A);
  assert(asset == nullptr);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple items
// ---------------------------------------------------------------------------
void test_multiple_items() {
  TEST(multiple_items);

  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));
  reg.add(&TAG_C, std::make_unique<TestAsset>(TAG_C));
  reg.add(&TAG_D, std::make_unique<TestAsset>(TAG_D));

  assert(reg.size() == 4);
  assert(reg.get(&TAG_A)->value() == 1);
  assert(reg.get(&TAG_B)->value() == 2);
  assert(reg.get(&TAG_C)->value() == 3);
  assert(reg.get(&TAG_D)->value() == 4);

  reg.set(&TAG_A, std::make_unique<TestAsset>(TAG_A, 100));
  reg.set(&TAG_D, std::make_unique<TestAsset>(TAG_D, 100));

  assert(reg.size() == 4);
  assert(reg.get(&TAG_A)->value() == 101);
  assert(reg.get(&TAG_B)->value() == 2);
  assert(reg.get(&TAG_C)->value() == 3);
  assert(reg.get(&TAG_D)->value() == 104);

  PASS();
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== ResourceRegistry Tests ===\n");

  test_empty_registry();
  test_add_and_get();
  test_duplicate_add();
  test_contains();
  test_remove();
  test_emplace();
  test_emplace_extra_args();
  test_set();
  test_get_entry();
  test_extract();
  test_for_each();
  test_get_all();
  test_clear();
  test_add_callback();
  test_remove_callback();
  test_clear_callbacks();
  test_get_missing();
  test_multiple_items();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
