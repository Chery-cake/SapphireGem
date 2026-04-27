#include "resource_registry.h"
#include <cassert>
#include <cstdio>
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

// ----- Common tag and asset types (used by all policies) -----

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

// Static tag instances (pointer stability is required)
static constexpr TestTag TAG_A{"alpha", 1};
static constexpr TestTag TAG_B{"beta", 2};
static constexpr TestTag TAG_C{"gamma", 3};
static constexpr TestTag TAG_D{"delta", 4};

// =========================================================================
// Tests for the UniquePtrPolicy (default)
// =========================================================================

void test_unique_empty_registry() {
  TEST(unique_empty_registry);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  assert(reg.empty());
  assert(reg.size() == 0);
  PASS();
}

void test_unique_add_and_get() {
  TEST(unique_add_and_get);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  bool added = reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(added);
  assert(reg.size() == 1);
  TestAsset *asset = reg.get(&TAG_A);
  assert(asset != nullptr);
  assert(asset->name() == "alpha");
  assert(asset->value() == 1);
  PASS();
}

void test_unique_duplicate_add() {
  TEST(unique_duplicate_add);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  bool duplicate = reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(!duplicate);
  assert(reg.size() == 1);
  PASS();
}

void test_unique_contains() {
  TEST(unique_contains);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(reg.contains(&TAG_A));
  assert(!reg.contains(&TAG_B));
  PASS();
}

void test_unique_remove() {
  TEST(unique_remove);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  bool removed = reg.remove(&TAG_A);
  assert(removed);
  assert(!reg.contains(&TAG_A));
  assert(reg.empty());
  bool notFound = reg.remove(&TAG_B);
  assert(!notFound);
  PASS();
}

void test_unique_emplace() {
  TEST(unique_emplace);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  bool emplaced = reg.emplace(&TAG_B);
  assert(emplaced);
  TestAsset *asset = reg.get(&TAG_B);
  assert(asset != nullptr);
  assert(asset->name() == "beta");
  assert(asset->value() == 2);
  PASS();
}

void test_unique_emplace_extra_args() {
  TEST(unique_emplace_extra_args);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.emplace(&TAG_C, 100);
  TestAsset *asset = reg.get(&TAG_C);
  assert(asset != nullptr);
  assert(asset->value() == 103); // gamma=3 + 100
  PASS();
}

void test_unique_set() {
  TEST(unique_set);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  bool wasNew = reg.set(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(wasNew);
  bool wasReplace = reg.set(&TAG_A, std::make_unique<TestAsset>(TAG_A, 100));
  assert(!wasReplace);
  TestAsset *asset = reg.get(&TAG_A);
  assert(asset->value() == 101); // alpha=1 + 100
  PASS();
}

void test_unique_get_entry() {
  TEST(unique_get_entry);
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

void test_unique_extract() {
  TEST(unique_extract);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  auto extracted = reg.extract(&TAG_A);
  assert(extracted != nullptr);
  assert(extracted->name() == "alpha");
  assert(reg.empty());
  auto missing = reg.extract(&TAG_B);
  assert(missing == nullptr);
  PASS();
}

void test_unique_for_each() {
  TEST(unique_for_each);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));
  int count = 0;
  reg.forEach([&count](const TestTag *, TestAsset *) { count++; });
  assert(count == 2);
  PASS();
}

void test_unique_get_all() {
  TEST(unique_get_all);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));
  reg.add(&TAG_C, std::make_unique<TestAsset>(TAG_C));
  auto all = reg.getAll();
  assert(all.size() == 3);
  PASS();
}

void test_unique_clear() {
  TEST(unique_clear);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.add(&TAG_B, std::make_unique<TestAsset>(TAG_B));
  reg.clear();
  assert(reg.empty());
  assert(reg.size() == 0);
  PASS();
}

void test_unique_add_callback() {
  TEST(unique_add_callback);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  bool callbackFired = false;
  const TestTag *receivedTag = nullptr;
  TestAsset *receivedAsset = nullptr;
  reg.onAdd([&](const TestTag *tag, TestAsset *asset) {
    callbackFired = true;
    receivedTag = tag;
    receivedAsset = asset;
  });
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  assert(callbackFired);
  assert(receivedTag == &TAG_A);
  assert(receivedAsset != nullptr);
  assert(receivedAsset->value() == 1);
  PASS();
}

void test_unique_remove_callback() {
  TEST(unique_remove_callback);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  bool callbackFired = false;
  const TestTag *receivedTag = nullptr;
  reg.onRemove([&](const TestTag *tag, TestAsset *asset) {
    callbackFired = true;
    receivedTag = tag;
    assert(asset->name() == "alpha");
  });
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.remove(&TAG_A);
  assert(callbackFired);
  assert(receivedTag == &TAG_A);
  PASS();
}

void test_unique_clear_callbacks() {
  TEST(unique_clear_callbacks);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  int counter = 0;
  reg.onAdd([&](const TestTag *, TestAsset *) { counter++; });
  reg.onRemove([&](const TestTag *, TestAsset *) { counter++; });
  reg.clearCallbacks();
  reg.add(&TAG_A, std::make_unique<TestAsset>(TAG_A));
  reg.remove(&TAG_A);
  assert(counter == 0);
  PASS();
}

void test_unique_get_missing() {
  TEST(unique_get_missing);
  core::ResourceRegistry<TestTag, TestAsset> reg;
  assert(reg.get(&TAG_A) == nullptr);
  PASS();
}

void test_unique_multiple_items() {
  TEST(unique_multiple_items);
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
  assert(reg.get(&TAG_A)->value() == 101);
  assert(reg.get(&TAG_D)->value() == 104);
  PASS();
}

// =========================================================================
// Tests for SharedPtrPolicy
// =========================================================================

using SharedRegistry =
    core::ResourceRegistry<TestTag, TestAsset,
                           core::SharedPtrPolicy<TestTag, TestAsset>>;

void test_shared_add_and_get() {
  TEST(shared_add_and_get);
  SharedRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  bool added = reg.add(&TAG_A, sp);
  assert(added);
  assert(sp.use_count() >= 2); // registry stores a shared_ptr
  auto got = reg.get(&TAG_A);
  assert(got != nullptr);
  assert(got == sp);
  assert(got->value() == 1);
  // release our local copy – registry still holds one
  sp.reset();
  got = reg.get(&TAG_A);
  assert(got != nullptr);
  assert(got.use_count() == 2); // got + internal
  PASS();
}

void test_shared_duplicate_add() {
  TEST(shared_duplicate_add);
  SharedRegistry reg;
  reg.add(&TAG_A, std::make_shared<TestAsset>(TAG_A));
  bool dup = reg.add(&TAG_A, std::make_shared<TestAsset>(TAG_A));
  assert(!dup);
  PASS();
}

void test_shared_remove() {
  TEST(shared_remove);
  SharedRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  assert(sp.use_count() >= 2);
  bool removed = reg.remove(&TAG_A);
  assert(removed);
  assert(sp.use_count() == 1); // only our local copy left
  PASS();
}

void test_shared_extract() {
  TEST(shared_extract);
  SharedRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  auto extracted = reg.extract(&TAG_A);
  assert(extracted == sp);
  assert(sp.use_count() == 2); // extracted + sp
  assert(reg.empty());
  PASS();
}

void test_shared_set() {
  TEST(shared_set);
  SharedRegistry reg;
  auto oldSp = std::make_shared<TestAsset>(TAG_A);
  bool wasNew = reg.set(&TAG_A, oldSp);
  assert(wasNew);
  // Replace with new asset
  auto newSp = std::make_shared<TestAsset>(TAG_A, 100);
  bool wasReplace = reg.set(&TAG_A, newSp);
  assert(!wasReplace);
  auto got = reg.get(&TAG_A);
  assert(got == newSp);
  assert(got->value() == 101);
  // oldSp still valid, just no longer in registry
  assert(oldSp.use_count() == 1);
  PASS();
}

void test_shared_callback() {
  TEST(shared_callback);
  SharedRegistry reg;
  const TestTag *tagInAdd = nullptr;
  TestAsset *assetInAdd = nullptr;
  const TestTag *tagInRemove = nullptr;
  TestAsset *assetInRemove = nullptr;
  reg.onAdd([&](const TestTag *t, TestAsset *a) {
    tagInAdd = t;
    assetInAdd = a;
  });
  reg.onRemove([&](const TestTag *t, TestAsset *a) {
    tagInRemove = t;
    assetInRemove = a;
  });
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  assert(tagInAdd == &TAG_A);
  assert(assetInAdd == sp.get());
  reg.remove(&TAG_A);
  assert(tagInRemove == &TAG_A);
  assert(assetInRemove == sp.get());
  PASS();
}

void test_shared_get_all() {
  TEST(shared_get_all);
  SharedRegistry reg;
  auto spA = std::make_shared<TestAsset>(TAG_A);
  auto spB = std::make_shared<TestAsset>(TAG_B);
  reg.add(&TAG_A, spA);
  reg.add(&TAG_B, spB);
  auto entries = reg.getAll();
  assert(entries.size() == 2);
  // The raw pointers must be valid
  bool foundA = false, foundB = false;
  for (auto &e : entries) {
    if (e.tag == &TAG_A) {
      assert(e.asset == spA.get());
      foundA = true;
    } else if (e.tag == &TAG_B) {
      assert(e.asset == spB.get());
      foundB = true;
    }
  }
  assert(foundA && foundB);
  PASS();
}

// =========================================================================
// Tests for WeakPtrPolicy
// =========================================================================

using WeakRegistry =
    core::ResourceRegistry<TestTag, TestAsset,
                           core::WeakPtrPolicy<TestTag, TestAsset>>;

void test_weak_add_and_get_lifetime() {
  TEST(weak_add_and_get_lifetime);
  WeakRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  bool added = reg.add(&TAG_A, sp);
  assert(added);
  auto got = reg.get(&TAG_A);
  assert(got != nullptr);
  assert(got == sp);
  sp.reset();
  auto stillThere = reg.get(&TAG_A);
  assert(stillThere == got);
  got.reset();
  stillThere.reset();
  auto gone = reg.get(&TAG_A);
  assert(gone == nullptr);
  PASS();
}

void test_weak_duplicate_handling() {
  TEST(weak_duplicate_handling);
  WeakRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  auto sp2 = std::make_shared<TestAsset>(TAG_A, 100);
  bool duplicate = reg.add(&TAG_A, sp2);
  assert(!duplicate);
  assert(reg.get(&TAG_A) == sp);
  PASS();
}

void test_weak_add_after_expiration() {
  TEST(weak_add_after_expiration);
  WeakRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  sp.reset();
  assert(reg.contains(&TAG_A)); // entry still exists
  auto gone = reg.get(&TAG_A);
  assert(gone == nullptr);
  auto sp2 = std::make_shared<TestAsset>(TAG_A, 50);
  bool addedAgain = reg.add(&TAG_A, sp2);
  assert(addedAgain);
  auto got = reg.get(&TAG_A);
  assert(got == sp2);
  assert(got->value() == 51); // alpha=1 + 50
  PASS();
}

void test_weak_remove() {
  TEST(weak_remove);
  WeakRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  bool removed = reg.remove(&TAG_A);
  assert(removed);
  assert(!reg.contains(&TAG_A));

  // Keep the asset alive with a local shared_ptr so the callback fires
  bool removeCb = false;
  reg.onRemove([&](const TestTag *, TestAsset *) { removeCb = true; });
  auto spB = std::make_shared<TestAsset>(TAG_B); // <-- keep alive
  reg.add(&TAG_B, spB);
  reg.remove(&TAG_B);
  assert(removeCb);

  // Test removing an expired entry (no callback expected)
  auto spC = std::make_shared<TestAsset>(TAG_C);
  reg.add(&TAG_C, spC);
  spC.reset(); // allow expiration
  bool removeCbExpired = false;
  reg.onRemove([&](const TestTag *, TestAsset *) { removeCbExpired = true; });
  bool removedExpired = reg.remove(&TAG_C);
  assert(removedExpired);
  assert(!reg.contains(&TAG_C));
  assert(!removeCbExpired); // no alive asset → no callback
  PASS();
}

void test_weak_extract() {
  TEST(weak_extract);
  WeakRegistry reg;
  auto sp = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, sp);
  auto extracted = reg.extract(&TAG_A);
  assert(extracted == sp);
  assert(extracted.use_count() == 2); // sp + extracted
  assert(!reg.contains(&TAG_A));

  // Expired extraction
  auto spB = std::make_shared<TestAsset>(TAG_B);
  reg.add(&TAG_B, spB);
  spB.reset(); // asset expires
  auto expiredExtract = reg.extract(&TAG_B);
  assert(expiredExtract == nullptr);
  assert(!reg.contains(&TAG_B));

  // Extraction while alive – callback should fire
  bool extractCb = false;
  reg.onRemove([&](const TestTag *, TestAsset *) { extractCb = true; });
  auto spC = std::make_shared<TestAsset>(TAG_C); // keep alive
  reg.add(&TAG_C, spC);
  auto tempExtract = reg.extract(&TAG_C);
  assert(tempExtract != nullptr);
  assert(extractCb);
  PASS();
}

void test_weak_set_replacement_callback() {
  TEST(weak_set_replacement_callback);
  WeakRegistry reg;
  int addCount = 0, removeCount = 0;
  reg.onAdd([&](const TestTag *, TestAsset *) { addCount++; });
  reg.onRemove([&](const TestTag *, TestAsset *) { removeCount++; });

  auto first = std::make_shared<TestAsset>(TAG_A);
  bool wasNew = reg.set(&TAG_A, first);
  assert(wasNew);
  assert(addCount == 1);
  assert(removeCount == 0);

  auto second = std::make_shared<TestAsset>(TAG_A, 100);
  bool wasReplace = reg.set(&TAG_A, second);
  assert(!wasReplace);
  assert(addCount == 2);
  assert(removeCount == 1);
  assert(reg.get(&TAG_A) == second);

  second.reset();
  assert(reg.get(&TAG_A) == nullptr);
  auto third = std::make_shared<TestAsset>(TAG_A, 200);
  bool wasReplace2 = reg.set(&TAG_A, third);
  assert(!wasReplace2);
  assert(addCount == 3);
  assert(removeCount == 1);
  assert(reg.get(&TAG_A) == third);
  PASS();
}

void test_weak_get_all_and_for_each() {
  TEST(weak_get_all_and_for_each);
  WeakRegistry reg;
  auto spA = std::make_shared<TestAsset>(TAG_A);
  auto spB = std::make_shared<TestAsset>(TAG_B);
  reg.add(&TAG_A, spA);
  reg.add(&TAG_B, spB);

  auto entries = reg.getAll();
  assert(entries.size() == 2);
  bool aOk = false, bOk = false;
  for (auto &e : entries) {
    if (e.tag == &TAG_A) {
      assert(e.asset != nullptr);
      assert(e.asset == spA.get());
      aOk = true;
    } else if (e.tag == &TAG_B) {
      assert(e.asset != nullptr);
      assert(e.asset == spB.get());
      bOk = true;
    }
  }
  assert(aOk && bOk);

  spA.reset();
  entries = reg.getAll();
  assert(entries.size() == 2);
  int nullCount = 0;
  for (auto &e : entries) {
    if (e.asset == nullptr)
      nullCount++;
  }
  assert(nullCount == 1);

  int forEachCount = 0;
  int forEachNull = 0;
  reg.forEach([&](const TestTag *tag, TestAsset *asset) {
    forEachCount++;
    if (asset == nullptr)
      forEachNull++;
  });
  assert(forEachCount == 2);
  assert(forEachNull == 1);
  PASS();
}

void test_weak_clear() {
  TEST(weak_clear);
  WeakRegistry reg;
  auto spA = std::make_shared<TestAsset>(TAG_A);
  reg.add(&TAG_A, spA);
  auto spB = std::make_shared<TestAsset>(TAG_B);
  reg.add(&TAG_B, spB);
  spB.reset();

  bool removeCbA = false, removeCbB = false;
  reg.onRemove([&](const TestTag *tag, TestAsset *asset) {
    if (tag == &TAG_A)
      removeCbA = true;
    if (tag == &TAG_B)
      removeCbB = true;
  });

  reg.clear();
  assert(reg.empty());
  assert(removeCbA == true);
  assert(removeCbB == false); // expired → no callback
  PASS();
}

// =========================================================================
// Main runner
// =========================================================================

int main() {
  std::printf("=== ResourceRegistry Tests (UniquePtrPolicy) ===\n");
  test_unique_empty_registry();
  test_unique_add_and_get();
  test_unique_duplicate_add();
  test_unique_contains();
  test_unique_remove();
  test_unique_emplace();
  test_unique_emplace_extra_args();
  test_unique_set();
  test_unique_get_entry();
  test_unique_extract();
  test_unique_for_each();
  test_unique_get_all();
  test_unique_clear();
  test_unique_add_callback();
  test_unique_remove_callback();
  test_unique_clear_callbacks();
  test_unique_get_missing();
  test_unique_multiple_items();

  std::printf("\n=== ResourceRegistry Tests (SharedPtrPolicy) ===\n");
  test_shared_add_and_get();
  test_shared_duplicate_add();
  test_shared_remove();
  test_shared_extract();
  test_shared_set();
  test_shared_callback();
  test_shared_get_all();

  std::printf("\n=== ResourceRegistry Tests (WeakPtrPolicy) ===\n");
  test_weak_add_and_get_lifetime();
  test_weak_duplicate_handling();
  test_weak_add_after_expiration();
  test_weak_remove();
  test_weak_extract();
  test_weak_set_replacement_callback();
  test_weak_get_all_and_for_each();
  test_weak_clear();

  std::printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
