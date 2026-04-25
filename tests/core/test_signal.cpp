#include "signal.hpp"
#include <cassert>
#include <cstdio>

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
// Test: Basic construction and initial state
// ---------------------------------------------------------------------------
void test_constructor() {
  TEST(constructor);

  core::signal::Signal<void()> sig;
  core::signal::SignalHub hub;

  assert(sig.empty());
  assert(sig.size() == 0);
  assert(hub.empty());
  assert(hub.size() == 0);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: connect() and basic emit()
// ---------------------------------------------------------------------------
void test_connect_and_emit() {
  TEST(connect_and_emit);
  core::signal::Signal<void(int)> sig;
  int counter = 0;

  auto result = sig.connect([&counter](int x) { counter += x; });
  assert(result.has_value());
  core::signal::ConnectionId id = *result;
  assert(id != 0);

  assert(!sig.empty());
  assert(sig.size() == 1);

  sig.emit(5);
  assert(counter == 5);

  sig.emit(3);
  assert(counter == 8);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: connect() with null slot returns error
// ---------------------------------------------------------------------------
void test_connect_null_slot() {
  TEST(connect_null_slot);
  core::signal::Signal<void()> sig;
  auto result = sig.connect(nullptr);
  assert(!result.has_value());
  assert(result.error() ==
         core::signal::Signal<void()>::ConnectError::NullSlot);
  assert(sig.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Test: disconnect() by ID
// ---------------------------------------------------------------------------
void test_disconnect() {
  TEST(disconnect);
  core::signal::Signal<void()> sig;
  int count = 0;
  auto id1 = *sig.connect([&count] { ++count; });
  auto id2 = *sig.connect([&count] { ++count; });

  assert(sig.size() == 2);

  bool removed = sig.disconnect(id1);
  assert(removed);
  assert(sig.size() == 1);

  sig.emit();
  assert(count == 1); // only second slot fired

  // Disconnect same ID again should return false
  removed = sig.disconnect(id1);
  assert(!removed);

  // Disconnect remaining
  sig.disconnect(id2);
  assert(sig.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Test: clear() removes all connections
// ---------------------------------------------------------------------------
void test_clear() {
  TEST(clear);
  core::signal::Signal<void()> sig;
  auto r = sig.connect([] {});
  r = sig.connect([] {});
  assert(sig.size() == 2);
  sig.clear();
  assert(sig.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Test: emit_until() stops when predicate returns true
// ---------------------------------------------------------------------------
void test_emit_until() {
  TEST(emit_until);
  core::signal::Signal<bool(int)> sig;
  int call_count = 0;
  auto r1 = sig.connect([&call_count](int x) -> bool {
    call_count++;
    return x > 10;
  });
  r1 = sig.connect([&call_count](int x) -> bool {
    call_count++;
    return x > 5;
  });
  r1 = sig.connect([&call_count](int x) -> bool {
    call_count++;
    return x > 0;
  });

  bool stopped = false;
  sig.emit_until(7, [&stopped](int handled) {
    stopped = handled;
    return handled; // stop if handled
  });
  assert(stopped);
  assert(call_count == 2);

  // Cleaner test:
  core::signal::Signal<int(int)> sig2;
  auto r2 = sig2.connect([](int) -> int { return 1; });
  r2 = sig2.connect([](int) -> int { return 2; });
  int result = 0;
  sig2.emit_until(0, [&result](int val) {
    result = val;
    return true; // stop immediately
  });
  assert(result == 1);
  PASS();
}

// ---------------------------------------------------------------------------
// Test: connection_ids() returns current connection IDs
// ---------------------------------------------------------------------------
void test_connection_ids() {
  TEST(connection_ids);
  core::signal::Signal<void()> sig;
  auto id1 = *sig.connect([] {});
  auto id2 = *sig.connect([] {});
  auto ids = sig.connection_ids();
  assert(ids.size() == 2);
  // Order not guaranteed, but both IDs should be present
  bool has1 = std::ranges::find(ids, id1) != ids.end();
  bool has2 = std::ranges::find(ids, id2) != ids.end();
  assert(has1 && has2);
  PASS();
}

// ---------------------------------------------------------------------------
// Test: ScopedConnection automatic disconnection
// ---------------------------------------------------------------------------
void test_scoped_connection_raii() {
  TEST(scoped_connection_raii);
  core::signal::Signal<void()> sig;
  int count = 0;
  {
    auto result = sig.connect([&count] { ++count; });
    core::signal::ScopedConnection<void()> conn(sig, *result);
    assert(conn.is_connected());
    sig.emit();
    assert(count == 1);
  } // conn destroyed, should disconnect
  sig.emit();
  assert(count == 1); // no additional call
  PASS();
}

// ---------------------------------------------------------------------------
// Test: ScopedConnection move semantics
// ---------------------------------------------------------------------------
void test_scoped_connection_move() {
  TEST(scoped_connection_move);
  core::signal::Signal<void()> sig;
  int count = 0;
  auto id = *sig.connect([&count] { ++count; });
  core::signal::ScopedConnection<void()> conn1(sig, id);
  assert(conn1.is_connected());

  core::signal::ScopedConnection<void()> conn2(std::move(conn1));
  assert(!conn1.is_connected()); // moved-from state
  assert(conn2.is_connected());

  sig.emit();
  assert(count == 1);

  // Move assignment
  core::signal::ScopedConnection<void()> conn3;
  conn3 = std::move(conn2);
  assert(!conn2.is_connected());
  assert(conn3.is_connected());

  sig.emit();
  assert(count == 2);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: ScopedConnection::reset() and release()
// ---------------------------------------------------------------------------
void test_scoped_connection_reset_release() {
  TEST(scoped_connection_reset_release);
  core::signal::Signal<void()> sig;
  int count = 0;
  auto id = *sig.connect([&count] { ++count; });
  core::signal::ScopedConnection<void()> conn(sig, id);
  assert(conn.is_connected());

  // reset() disconnects
  conn.reset();
  assert(!conn.is_connected());
  sig.emit();
  assert(count == 0);

  // Reconnect for release test
  id = *sig.connect([&count] { ++count; });
  conn = core::signal::ScopedConnection<void()>(sig, id);
  core::signal::ConnectionId released_id = conn.release();
  assert(!conn.is_connected());
  assert(released_id == id);
  sig.emit();
  assert(count == 1); // still connected
  sig.disconnect(id); // manual cleanup
  PASS();
}

// ---------------------------------------------------------------------------
// Test: SignalHub connects and manages multiple signals
// ---------------------------------------------------------------------------
void test_signal_hub_connect() {
  TEST(signal_hub_connect);
  core::signal::Signal<void()> sig1, sig2;
  core::signal::SignalHub hub;
  int count1 = 0, count2 = 0;

  auto conn1 = hub.connect(sig1, [&count1] { ++count1; });
  auto conn2 = hub.connect(sig2, [&count2] { ++count2; });

  assert(hub.size() == 2);
  assert(!hub.empty());

  sig1.emit();
  assert(count1 == 1);
  sig2.emit();
  assert(count2 == 1);

  // conn1 and conn2 are ScopedConnections; they can be manually reset
  conn1.reset();
  sig1.emit();
  assert(count1 == 1); // not incremented

  // Hub still tracks conn2
  assert(hub.size() == 2); // wait, hub's internal count is number of
                           // disconnectors, not active connections
  // Actually, hub.size() returns number of stored disconnector functions,
  // which is 2. Even after manual disconnect, the disconnector remains (it
  // just becomes no-op). That's acceptable.

  PASS();
}

// ---------------------------------------------------------------------------
// Test: SignalHub::clear() disconnects all
// ---------------------------------------------------------------------------
void test_signal_hub_clear() {
  TEST(signal_hub_clear);
  core::signal::Signal<void()> sig1, sig2;
  core::signal::SignalHub hub;
  int count1 = 0, count2 = 0;

  hub.connect(sig1, [&count1] { ++count1; });
  hub.connect(sig2, [&count2] { ++count2; });

  sig1.emit();
  sig2.emit();
  assert(count1 == 1 && count2 == 1);

  hub.clear();
  sig1.emit();
  sig2.emit();
  assert(count1 == 1 && count2 == 1); // no change
  assert(hub.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Test: SignalHub::add_disconnector() custom cleanup
// ---------------------------------------------------------------------------
void test_signal_hub_add_disconnector() {
  TEST(signal_hub_add_disconnector);
  core::signal::SignalHub hub;
  bool cleaned = false;
  hub.add_disconnector([&cleaned] { cleaned = true; });
  assert(!cleaned);
  hub.clear();
  assert(cleaned);
  PASS();
}

// ---------------------------------------------------------------------------
// Test: Multiple emissions and thread safety (basic)
// ---------------------------------------------------------------------------
void test_multiple_emissions() {
  TEST(multiple_emissions);
  core::signal::Signal<void(int)> sig;
  std::atomic<int> sum{0};
  const int N = 10;

  for (int i = 0; i < N; ++i) {
    auto r = sig.connect([&sum](int x) { sum += x; });
  }

  // Sequential emit
  sig.emit(2);
  assert(sum == N * 2);

  sum = 0;
  // Parallel emit (requires slots to be thread-safe; our slot uses atomic)
  sig.emit_parallel(3);
  // Allow threads to finish (emit_parallel waits for completion of parallel
  // algorithm)
  assert(sum == N * 3);

  PASS();
}

// ---------------------------------------------------------------------------
// Test: emit_until with early exit
// ---------------------------------------------------------------------------
void test_emit_until_early_exit() {
  TEST(emit_until_early_exit);
  core::signal::Signal<int(int)> sig;
  auto r = sig.connect([](int x) -> int { return x * 2; });
  r = sig.connect([](int x) -> int { return x * 3; });
  int result = 0;
  sig.emit_until(5, [&result](int val) {
    result = val;
    return val > 10; // stop when >10
  });
  // First slot returns 10, not >10, so continue.
  // Second slot returns 15, >10, so stop.
  assert(result == 15);
  PASS();
}

// ---------------------------------------------------------------------------
// Test: connection_ids after clear
// ---------------------------------------------------------------------------
void test_connection_ids_empty() {
  TEST(connection_ids_empty);
  core::signal::Signal<void()> sig;
  auto ids = sig.connection_ids();
  assert(ids.empty());
  auto r = sig.connect([] {});
  ids = sig.connection_ids();
  assert(ids.size() == 1);
  sig.clear();
  ids = sig.connection_ids();
  assert(ids.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Test: ScopedConnection default constructed
// ---------------------------------------------------------------------------
void test_scoped_connection_default() {
  TEST(scoped_connection_default);
  core::signal::ScopedConnection<void()> conn;
  assert(!conn.is_connected());
  conn.reset(); // no-op
  core::signal::ConnectionId id = conn.release();
  assert(id == 0);
  PASS();
}

// ---------------------------------------------------------------------------
// Test: Hub Non-auto-disconect behaviour
// ---------------------------------------------------------------------------
void test_hub_connection_keeps_alive() {
  TEST(hub_connection_keeps_alive);
  core::signal::Signal<void()> sig;
  core::signal::SignalHub hub;
  int count = 0;

  // discard return value
  hub.connect(sig, [&count] { ++count; });
  sig.emit();
  assert(count == 1); // still connected
  hub.clear();
  sig.emit();
  assert(count == 1); // disconnected by hub
  PASS();
}

void test_hub_connection_move_preserves_flag() {
  TEST(hub_connection_move_preserves_flag);
  core::signal::Signal<void()> sig;
  core::signal::SignalHub hub;
  int count = 0;
  auto conn1 = hub.connect(sig, [&count] { ++count; });
  // Move it away
  core::signal::ScopedConnection<void()> conn2(std::move(conn1));
  sig.emit();
  assert(count == 1); // still alive
  PASS();
}

void test_emit_empty_signal() {
  TEST(emit_empty_signal);
  core::signal::Signal<void()> sig;
  sig.emit();          // must not crash
  sig.emit_parallel(); // must not crash
  bool predicate_called = false;
  sig.emit_until([&] {
    predicate_called = true;
    return true;
  });
  assert(!predicate_called); // nothing to call
  PASS();
}

void test_emit_until_no_stop() {
  TEST(emit_until_no_stop);
  core::signal::Signal<int(int)> sig;
  int call_count = 0;
  auto res = sig.connect([&](int x) {
    call_count++;
    return x;
  });
  res = sig.connect([&](int x) {
    call_count++;
    return x;
  });
  sig.emit_until(0, [](int) { return false; }); // never stops
  assert(call_count == 2);                      // all slots called
  PASS();
}

void test_clear_preserves_id_monotonicity() {
  TEST(clear_preserves_id_monotonicity);
  core::signal::Signal<void()> sig;
  auto id1 = *sig.connect([] {});
  sig.clear();
  auto id2 = *sig.connect([] {});
  assert(id2 > id1); // no reuse
  PASS();
}

void test_hub_after_manual_reset() {
  TEST(hub_after_manual_reset);
  core::signal::Signal<void()> sig;
  core::signal::SignalHub hub;
  int a = 0, b = 0;
  auto c1 = hub.connect(sig, [&] { ++a; });
  auto c2 = hub.connect(sig, [&] { ++b; });
  c1.reset();              // disconnect manually, hub still has 2 disconnectors
  assert(hub.size() == 2); // internal count unchanged
  sig.emit();
  assert(a == 0 && b == 1); // only second slot fires
  hub.clear();              // should not crash, second slot disconnected
  sig.emit();
  assert(b == 1);
  assert(hub.empty());
  PASS();
}

// ---------------------------------------------------------------------------
// Run all tests
// ---------------------------------------------------------------------------
int main() {
  std::printf("=== Signal Tests ===\n");

  test_constructor();
  test_connect_and_emit();
  test_connect_null_slot();
  test_disconnect();
  test_clear();
  test_emit_until();
  test_connection_ids();
  test_scoped_connection_raii();
  test_scoped_connection_move();
  test_scoped_connection_reset_release();
  test_signal_hub_connect();
  test_signal_hub_clear();
  test_signal_hub_add_disconnector();
  test_multiple_emissions();
  test_emit_until_early_exit();
  test_connection_ids_empty();
  test_scoped_connection_default();
  test_hub_connection_keeps_alive();
  test_hub_connection_move_preserves_flag();
  test_emit_empty_signal();
  test_emit_until_no_stop();
  test_clear_preserves_id_monotonicity();
  test_hub_after_manual_reset();

  std::printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return (tests_passed == tests_run) ? 0 : 1;
}
