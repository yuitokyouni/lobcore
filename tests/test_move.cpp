#include <catch2/catch_test_macros.hpp>

#include <lobcore/book.hpp>
#include <lobcore/event_log.hpp>

#include "test_support.hpp"

using namespace lobcore;

namespace {

std::vector<Trade> submit(OrderBook& book, const Order& order) {
  return book.add_limit(order, order.decided_at);
}

Order buy(OrderId id, Price p, Qty q) {
  const Timestamp t = test_support::next_timestamp();
  return Order{id, Side::Buy, p, q, t};
}

Order sell(OrderId id, Price p, Qty q) {
  const Timestamp t = test_support::next_timestamp();
  return Order{id, Side::Sell, p, q, t};
}

constexpr int kDeepLevels     = 100;
constexpr int kOrdersPerLevel = 10;

OrderBook make_deep_book() {
  OrderBook book;
  OrderId id = 1;
  for (int level = 0; level < kDeepLevels; ++level) {
    const Price bid_px = 1000 - level;
    const Price ask_px = 1001 + level;
    for (int n = 0; n < kOrdersPerLevel; ++n) {
      submit(book, buy(id++, bid_px, 1));
      submit(book, sell(id++, ask_px, 1));
    }
  }
  return book;
}

}  // namespace

TEST_CASE("OrderBook copy constructor duplicates pool-backed state") {
  OrderBook original;
  submit(original, buy(1, 100, 4));
  submit(original, sell(2, 101, 2));

  const OrderBook copy(original);

  CHECK(copy.state_hash() == original.state_hash());
  CHECK(copy.remaining(1) == 4);
  CHECK(copy.remaining(2) == 2);
  CHECK(copy.locations_consistent());
}

TEST_CASE("OrderBook move constructor transfers resting orders") {
  OrderBook original;
  submit(original, sell(1, 101, 5));
  const auto hash_before = original.state_hash();

  OrderBook moved(std::move(original));

  CHECK(moved.state_hash() == hash_before);
  CHECK(moved.remaining(1) == 5);
  CHECK(moved.locations_consistent());
}

TEST_CASE("OrderBook returned by value remains usable after move assignment") {
  auto build = [] { return make_deep_book(); };

  OrderBook book;
  book = build();

  const Timestamp ts = test_support::next_timestamp();
  const auto      trades =
      book.add_limit(Order{9'999'999, Side::Buy, 2000, 50, ts}, ts);
  REQUIRE(trades.size() == 50);
  CHECK(book.locations_consistent());
  CHECK(book.best_ask().has_value());
}

TEST_CASE("OrderBook move assignment swaps ownership and leaves source empty") {
  OrderBook populated;
  submit(populated, sell(1, 101, 5));

  OrderBook empty;
  const auto hash_before = populated.state_hash();

  empty = std::move(populated);

  CHECK(empty.state_hash() == hash_before);
  CHECK(empty.remaining(1) == 5);
  CHECK_FALSE(populated.best_bid().has_value());
  CHECK_FALSE(populated.best_ask().has_value());
  CHECK(populated.locations_consistent());
  CHECK(empty.locations_consistent());

  submit(populated, buy(2, 100, 1));
  CHECK(populated.best_bid().has_value());
}

TEST_CASE("OrderBook copy assignment duplicates pool-backed state") {
  OrderBook original;
  submit(original, buy(1, 100, 4));
  submit(original, sell(2, 101, 2));

  OrderBook copy;
  copy = original;

  CHECK(copy.state_hash() == original.state_hash());
  CHECK(copy.remaining(1) == 4);
  CHECK(copy.remaining(2) == 2);

  REQUIRE(copy.cancel(1));
  submit(original, buy(3, 99, 1));

  CHECK_FALSE(copy.remaining(1).has_value());
  CHECK(original.remaining(1) == 4);
}

TEST_CASE("replay return value supports further book operations") {
  LoggedBook logged;
  logged.add_limit(buy(1, 100, 10), 1);
  logged.add_limit(sell(2, 100, 4), 2);
  logged.add_limit(sell(3, 101, 3), 3);
  logged.cancel(3, 4);

  OrderBook book = replay(logged.log());
  const auto hash_after_replay = book.state_hash();

  const Timestamp ts = test_support::next_timestamp();
  const auto      trades =
      book.add_limit(Order{4, Side::Buy, 101, 2, ts}, ts);
  REQUIRE(trades.empty());
  CHECK(book.remaining(4) == 2);
  CHECK(book.cancel(1));
  CHECK(book.state_hash() != hash_after_replay);
  CHECK(book.locations_consistent());
}
