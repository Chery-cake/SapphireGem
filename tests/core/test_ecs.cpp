#include "entities.h"
#include "signal.hpp"
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
// Components (some with default values, some without)
// ---------------------------------------------------------------------------
struct Position {
  float x = 0, y = 0;
};
struct Velocity {
  float dx = 0, dy = 0;
};
struct Health {
  int hp = 100;
};
struct Mana {
  int mp = 50;
};
struct Name {
  std::string name;
};

// Component that tracks destruction count (global)
static int destr_counter = 0;
struct Tracked {
  int id;
  Tracked(int i = -1) : id(i) {}
  ~Tracked() { ++destr_counter; }
};

// ---------------------------------------------------------------------------
// Tuple tests
// ---------------------------------------------------------------------------
void test_tuple_constructor_forwarding() {
  TEST(tuple_forwarding_constructor);
  ecs::entity::Tuple<Position, Velocity> t(Position(3, 4), Velocity(1, 2));
  assert(t.get<Position>().x == 3);
  assert(t.get<Position>().y == 4);
  assert(t.get<Velocity>().dx == 1);
  assert(t.get<Velocity>().dy == 2);
  PASS();
}

void test_tuple_default_constructor() {
  TEST(tuple_default_constructor);
  ecs::entity::Tuple<Position, Velocity> t;
  assert(t.get<Position>().x == 0); // relies on default member values
  PASS();
}

void test_tuple_destruction() {
  TEST(tuple_destruction);
  destr_counter = 0;
  {
    ecs::entity::Tuple<Tracked> t(Tracked(10));
    destr_counter = 0;
  }
  assert(destr_counter == 1); // t's Tracked destroyed
  PASS();
}

void test_tuple_data() {
  TEST(tuple_data);

  ecs::entity::Tuple<Position, Velocity> t(Position(1, 2), Velocity(3, 4));

  assert(t.get<Position>().x == 1);
  assert(t.get<Velocity>().dx == 3);

  t.get<Position>().x = 5;
  t.get<Velocity>().dx = 10;

  assert(t.get<Position>().x == 5);
  assert(t.get<Velocity>().dx == 10);

  PASS();
}

// ---------------------------------------------------------------------------
// Linear tests
// ---------------------------------------------------------------------------
void test_linear_constructor_forwarding() {
  TEST(linear_forwarding_constructor);
  struct TestEnt : public ecs::entity::Linear<TestEnt, Position, Health> {
    // Forward to Linear's constructor
    using ecs::entity::Linear<TestEnt, Position, Health>::Linear;
  };
  TestEnt e(Position(9, 9), Health(42));
  assert(e.get<Position>().x == 9);
  assert(e.get<Health>().hp == 42);
  PASS();
}

void test_linear_destruction() {
  TEST(linear_destruction);
  destr_counter = 0;
  {
    struct TrackedEnt : public ecs::entity::Linear<TrackedEnt, Tracked> {
      using ecs::entity::Linear<TrackedEnt, Tracked>::Linear;
    };
    TrackedEnt e(Tracked(5));
    destr_counter = 0;
  }
  assert(destr_counter == 1);
  PASS();
}

void test_linear_data() {
  TEST(linear_data);
  struct TestEnt : public ecs::entity::Linear<TestEnt, Position, Health> {
    // Forward to Linear's constructor
    using ecs::entity::Linear<TestEnt, Position, Health>::Linear;
    void takeDamage(int dmg) { Health::hp -= dmg; }
  };
  TestEnt e(Position(9, 9), Health(50));
  assert(e.get<Health>().hp == 50);
  e.takeDamage(20);
  assert(e.get<Health>().hp == 30);
  PASS();
}

// ---------------------------------------------------------------------------
// Virtual tests
// ---------------------------------------------------------------------------
void test_virtual_constructor_forwarding() {
  TEST(virtual_forwarding_constructor);
  struct TestRole
      : public virtual ecs::entity::Virtual<TestRole, Position, Mana> {
    using ecs::entity::Virtual<TestRole, Position, Mana>::Virtual;
  };
  // Most derived class must initialise virtual bases
  struct TestDerived : public TestRole {
    TestDerived(Position p, Mana m) : Position(p), Mana(m) {}
  };
  TestDerived d(Position(7, 7), Mana(88));
  assert(d.get<Position>().x == 7);
  assert(d.get<Mana>().mp == 88);
  PASS();
}

void test_virtual_destruction() {
  TEST(virtual_destruction);
  destr_counter = 0;
  {
    struct TrackedVirt
        : public virtual ecs::entity::Virtual<TrackedVirt, Tracked> {
      TrackedVirt(Tracked t) : Tracked(t) {}
    };
    struct Derived : public TrackedVirt {
      Derived(Tracked t) : Tracked(t), TrackedVirt(t) {}
    };
    Derived d(Tracked{77});
    destr_counter = 0;
  }
  assert(destr_counter == 1);
  PASS();
}

void test_virtual_multiple_inheritance() {
  TEST(virtual_multiple_inheritance);
  struct Mage : public virtual ecs::entity::Virtual<Mage, Position, Mana> {
    using ecs::entity::Virtual<Mage, Position, Mana>::Virtual;
    void castSpell() { Mana::mp -= 10; }
  };
  struct Fighter
      : public virtual ecs::entity::Virtual<Fighter, Position, Health> {
    using ecs::entity::Virtual<Fighter, Position, Health>::Virtual;
    void takeDamage(int dmg) { Health::hp -= dmg; }
  };
  // Most derived class must initialise virtual bases
  struct TestDerived : public Mage, public Fighter {
    TestDerived(Position p, Mana m, Health h)
        : Position(p), Mana(m), Health(h) {}
  };
  TestDerived d(Position(7, 7), Mana(100), Health(100));
  assert(d.Mage::get<Position>().x == 7);
  assert(d.Fighter::get<Position>().x == 7);
  assert(d.Mage::get<Position>().x == d.Fighter::get<Position>().x);
  assert(d.Mage::get<Mana>().mp == 100);
  assert(d.Fighter::get<Health>().hp == 100);
  d.castSpell();
  d.takeDamage(20);
  assert(d.Mage::get<Mana>().mp == 90);
  assert(d.Fighter::get<Health>().hp == 80);
  PASS();
}

// ---------------------------------------------------------------------------
// Combined tests with multiple patterns
// ---------------------------------------------------------------------------
void test_linear_derived_destruction_order() {
  TEST(Linear_derived_destruction_order);
  destr_counter = 0;
  {
    struct BaseEnt : public ecs::entity::Linear<BaseEnt, Tracked> {
      using ecs::entity::Linear<BaseEnt, Tracked>::Linear;
    };
    struct DerivedEnt : public BaseEnt {
      Tracked extra;
      DerivedEnt(Tracked t1, Tracked t2) : BaseEnt(std::move(t1)), extra(t2) {}
    };
    DerivedEnt e(Tracked(1), Tracked(2));
    destr_counter = 0;
  }
  // Both Tracked objects destroyed
  assert(destr_counter == 2);
  PASS();
}

void test_virtual_multiple_inheritance_destruction() {
  TEST(Virtual_MI_destruction);
  destr_counter = 0;
  {
    struct RoleA : public virtual ecs::entity::Virtual<RoleA, Tracked> {
      RoleA(Tracked t) : Tracked(t) {}
    };
    struct RoleB : public virtual ecs::entity::Virtual<RoleB, Tracked> {
      RoleB(Tracked t) : Tracked(t) {}
    };
    struct Combined : public RoleA, public RoleB {
      Combined(Tracked t) : Tracked(t), RoleA(t), RoleB(t) {}
    };
    Combined c(Tracked{99});
    destr_counter = 0;
  }
  // Only one Tracked instance shared, so destructor called once
  assert(destr_counter == 1);
  PASS();
}

// ---------------------------------------------------------------------------
// Test Dynamic storage component (attached to any entity)
// ---------------------------------------------------------------------------
void test_dynamic_storage() {
  TEST(dynamic_storage);

  struct DynamicEntity
      : public ecs::entity::Tuple<Position, ecs::component::DynamicStorage> {};

  DynamicEntity e;
  e.get<ecs::component::DynamicStorage>().add<std::string>("treasure");
  assert(e.get<ecs::component::DynamicStorage>().has<std::string>());
  auto *label = e.get<ecs::component::DynamicStorage>().get<std::string>();
  assert(*label == "treasure");
  e.get<ecs::component::DynamicStorage>().remove<std::string>();
  assert(!e.get<ecs::component::DynamicStorage>().has<std::string>());

  e.get<ecs::component::DynamicStorage>().add<std::string>("treasure");
  e.get<ecs::component::DynamicStorage>().add<std::string>("treasure2");
  assert(e.get<ecs::component::DynamicStorage>().has<std::string>());
  label = e.get<ecs::component::DynamicStorage>().get<std::string>();
  assert(*label == "treasure2");
  e.get<ecs::component::DynamicStorage>().remove<std::string>();
  assert(!e.get<ecs::component::DynamicStorage>().has<std::string>());

  PASS();
}

// ---------------------------------------------------------------------------
// Test 6: Signal communication between entities
// ---------------------------------------------------------------------------
void test_signal_communication() {
  TEST(signal_communication);

  struct Emitter : public ecs::entity::Tuple<ecs::component::SignalHub> {};
  struct Receiver : public ecs::entity::Tuple<ecs::component::SignalHub, Name> {
  };

  Emitter emitter;
  Receiver receiver;
  receiver.get<Name>().name = "unset";

  core::signal::Signal<void(const std::string &)> messageSignal;
  receiver.get<ecs::component::SignalHub>()
      .connect(messageSignal,
               [&receiver](const std::string &msg) {
                 receiver.get<Name>().name = msg;
               })
      .release();
  messageSignal.emit("hello");
  assert(receiver.get<Name>().name == "hello");
  PASS();
}

// ---------------------------------------------------------------------------
// Test const access
// ---------------------------------------------------------------------------
void test_const_access() {
  TEST(const_access);
  const ecs::entity::Tuple<Position, Velocity> t(Position{1, 2},
                                                 Velocity{3, 4});
  assert(t.get<Position>().x == 1);
  assert(t.get<Velocity>().dy == 4);
  PASS();
}

// ---------------------------------------------------------------------------
// Test constructors
// ---------------------------------------------------------------------------
void test_linear_default_constructor() {
  TEST(linear_default_constructor);
  struct DefaultEnt
      : public ecs::entity::Linear<DefaultEnt, Position, Velocity> {
    using ecs::entity::Linear<DefaultEnt, Position, Velocity>::Linear;
  };
  DefaultEnt e; // calls Linear default constructor
  assert(e.get<Position>().x == 0);
  assert(e.get<Velocity>().dy == 0);
  PASS();
}

void test_virtual_default_constructor() {
  TEST(virtual_default_constructor);
  struct DefaultRole
      : public virtual ecs::entity::Virtual<DefaultRole, Position, Velocity> {
    using ecs::entity::Virtual<DefaultRole, Position, Velocity>::Virtual;
  };
  // The most‑derived class must still initialise virtual bases if it has
  // user‑defined constructor, but here we rely on default construction
  // because DefaultFinal provides no user‑defined constructor.
  struct DefaultFinal : public DefaultRole {};
  DefaultFinal df; // virtual bases are default‑initialised
  assert(df.get<Position>().x == 0);
  assert(df.get<Velocity>().dy == 0);
  PASS();
}

void test_tuple_copy() {
  TEST(tuple_copy);
  ecs::entity::Tuple<Position, Velocity> original(Position{1, 2},
                                                  Velocity{3, 4});
  auto copy = original; // copy constructor
  assert(copy.get<Position>().x == 1);
  assert(copy.get<Velocity>().dy == 4);
  // Modify the copy; original stays unchanged
  copy.get<Position>().x = 99;
  assert(original.get<Position>().x == 1);
  PASS();
}

void test_tuple_move() {
  TEST(tuple_move);
  ecs::entity::Tuple<Position, Velocity> original(Position{1, 2},
                                                  Velocity{3, 4});
  auto moved = std::move(original); // move constructor
  assert(moved.get<Position>().x == 1);
  // original is in a valid but unspecified state; we don't test further
  PASS();
}

void test_linear_copy() {
  TEST(linear_copy);
  struct TestEnt : public ecs::entity::Linear<TestEnt, Position, Health> {
    using ecs::entity::Linear<TestEnt, Position, Health>::Linear;
  };
  TestEnt e(Position{1, 2}, Health{30});
  TestEnt copy = e; // copy constructor
  assert(copy.get<Position>().x == 1);
  assert(copy.get<Health>().hp == 30);
  copy.get<Health>().hp = 99;
  assert(e.get<Health>().hp == 30);
  PASS();
}

void test_virtual_copy() {
  TEST(virtual_copy);
  struct TestRole
      : public virtual ecs::entity::Virtual<TestRole, Position, Mana> {
    using Virtual::Virtual;
  };
  // Since virtual bases are shared, the most‑derived class must handle
  // copying
  struct TestDerived : public TestRole {
    TestDerived(Position p, Mana m) : Position(p), Mana(m) {}
  };
  TestDerived d(Position{1, 2}, Mana{10});
  TestDerived copy =
      d; // copy constructor (works because components are trivially copyable)
  assert(copy.get<Position>().x == 1);
  assert(copy.get<Mana>().mp == 10);
  PASS();
}

// ---------------------------------------------------------------------------
// Run all tests
// ---------------------------------------------------------------------------
int main() {
  std::printf("=== ECS Tests ===\n");

  test_tuple_constructor_forwarding();
  test_tuple_default_constructor();
  test_tuple_destruction();
  test_tuple_data();
  test_linear_constructor_forwarding();
  test_linear_destruction();
  test_linear_data();
  test_virtual_constructor_forwarding();
  test_virtual_destruction();
  test_virtual_multiple_inheritance();
  test_linear_derived_destruction_order();
  test_virtual_multiple_inheritance_destruction();
  test_dynamic_storage();
  test_signal_communication();
  test_const_access();

  test_linear_default_constructor();
  test_virtual_default_constructor();
  test_tuple_copy();
  test_tuple_move();
  test_linear_copy();
  test_virtual_copy();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
