#include <catch2/catch_test_macros.hpp>

#include <lobcore/event_log.hpp>

using namespace lobcore;

namespace {
Order buy_at(OrderId id, Price p, Qty q, Timestamp decided_at) {
  return Order{id, Side::Buy, p, q, decided_at};
}

Order sell_at(OrderId id, Price p, Qty q, Timestamp decided_at) {
  return Order{id, Side::Sell, p, q, decided_at};
}
}  // namespace

TEST_CASE("logged add produces one Add record for resting order") {
  LoggedBook logged;
  logged.add_limit(buy_at(1, 100, 10, 1), 1);

  REQUIRE(logged.log().size() == 1);
  CHECK(logged.log()[0].kind == EventKind::Add);
  CHECK(logged.log()[0].seq == 1);
  CHECK(logged.log()[0].order_id == 1);
  CHECK(logged.log()[0].qty == 10);
}

TEST_CASE("logged sweep produces Add and Fill records") {
  LoggedBook logged;
  logged.add_limit(sell_at(1, 100, 3, 1), 1);
  logged.add_limit(sell_at(2, 101, 3, 2), 2);
  logged.add_limit(sell_at(3, 102, 3, 3), 3);
  logged.add_limit(buy_at(4, 102, 7, 4), 4);

  REQUIRE(logged.log().size() == 7);
  CHECK(logged.log()[0].kind == EventKind::Add);
  CHECK(logged.log()[1].kind == EventKind::Add);
  CHECK(logged.log()[2].kind == EventKind::Add);
  CHECK(logged.log()[3].kind == EventKind::Add);
  CHECK(logged.log()[3].seq == 4);
  CHECK(logged.log()[4].kind == EventKind::Fill);
  CHECK(logged.log()[5].kind == EventKind::Fill);
  CHECK(logged.log()[6].kind == EventKind::Fill);
  CHECK(logged.log()[4].seq == 4);
  CHECK(logged.log()[5].seq == 4);
  CHECK(logged.log()[6].seq == 4);
}

TEST_CASE("logged reject emits Reject without consuming acceptance seq") {
  LoggedBook logged;
  logged.add_limit(buy_at(1, 100, 0, 1), 1);
  logged.add_limit(buy_at(2, 100, 5, 2), 2);

  REQUIRE(logged.log().size() == 2);
  CHECK(logged.log()[0].kind == EventKind::Reject);
  CHECK(logged.log()[0].seq == 0);
  CHECK(logged.log()[0].reason == RejectReason::NonPositiveQty);
  CHECK(logged.log()[1].kind == EventKind::Add);
  CHECK(logged.log()[1].seq == 1);
}

TEST_CASE("logged Add captures best bid and ask at receipt") {
  LoggedBook logged;
  logged.add_limit(buy_at(1, 100, 10, 1), 1);
  logged.add_limit(sell_at(2, 101, 5, 2), 2);

  const auto& add = logged.log()[1];
  REQUIRE(add.kind == EventKind::Add);
  CHECK(add.best_bid_price == 100);
  CHECK(add.best_bid_qty == 10);
  CHECK(add.best_ask_price == 0);
  CHECK(add.best_ask_qty == 0);
}

TEST_CASE("replay reproduces book state hash") {
  LoggedBook logged;
  logged.add_limit(buy_at(1, 100, 10, 1), 1);
  logged.add_limit(sell_at(2, 100, 4, 2), 2);   // partial fill
  logged.add_limit(sell_at(3, 101, 3, 3), 3);
  logged.add_limit(sell_at(4, 100, 3, 4), 4);
  logged.add_limit(sell_at(5, 102, 3, 5), 5);
  logged.add_limit(buy_at(6, 102, 7, 6), 6);    // sweep (3 fills)
  logged.add_limit(sell_at(1, 99, 1, 7), 7);    // reject: duplicate ID
  logged.add_limit(buy_at(7, 100, 0, 8), 8);    // reject: non-positive qty
  logged.cancel(5, 9);                          // cancel resting remainder

  const auto original = logged.book();
  const auto replayed = replay(logged.log());
  CHECK(replayed.state_hash() == original.state_hash());
  CHECK(replayed.locations_consistent());
  CHECK(replayed.rejects().duplicate_order_id == original.rejects().duplicate_order_id);
  CHECK(replayed.rejects().non_positive_qty == original.rejects().non_positive_qty);
  CHECK(replayed.rejects().non_monotonic_timestamp == original.rejects().non_monotonic_timestamp);
}
