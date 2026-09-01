// Stage 1 の仕様書。全部通ったら Stage 1 完了。
#include <catch2/catch_test_macros.hpp>

#include <lobcore/book.hpp>

#include "test_support.hpp"

using namespace lobcore;

namespace {
Order buy(OrderId id, Price p, Qty q) {
  const Timestamp t = test_support::next_timestamp();
  return Order{id, Side::Buy, p, q, t};
}

Order sell(OrderId id, Price p, Qty q) {
  const Timestamp t = test_support::next_timestamp();
  return Order{id, Side::Sell, p, q, t};
}

Order buy_at(OrderId id, Price p, Qty q, Timestamp decided_at) {
  return Order{id, Side::Buy, p, q, decided_at};
}

Order sell_at(OrderId id, Price p, Qty q, Timestamp decided_at) {
  return Order{id, Side::Sell, p, q, decided_at};
}
}  // namespace

TEST_CASE("empty book has no best levels") {
  OrderBook book;
  CHECK_FALSE(book.best_bid().has_value());
  CHECK_FALSE(book.best_ask().has_value());
}

TEST_CASE("resting bid sets best bid without trading") {
  OrderBook book;
  auto trades = book.add_limit(buy(1, 100, 10));
  CHECK(trades.empty());
  REQUIRE(book.best_bid().has_value());
  CHECK(book.best_bid()->price == 100);
  CHECK(book.best_bid()->qty == 10);
  CHECK_FALSE(book.best_ask().has_value());
  CHECK(book.remaining(1).value_or(-1) == 10);
}

TEST_CASE("crossing sell fills at maker price") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  auto trades = book.add_limit(sell(2, 99, 10));
  REQUIRE(trades.size() == 1);
  CHECK(trades[0].maker_id == 1);
  CHECK(trades[0].taker_id == 2);
  CHECK(trades[0].price == 100);  // maker の指値。taker の 99 ではない
  CHECK(trades[0].qty == 10);
  CHECK_FALSE(book.best_bid().has_value());
  CHECK_FALSE(book.best_ask().has_value());
}

TEST_CASE("partial fill leaves maker remainder resting") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  auto trades = book.add_limit(sell(2, 100, 4));
  REQUIRE(trades.size() == 1);
  CHECK(trades[0].qty == 4);
  CHECK(book.remaining(1).value_or(-1) == 6);
  CHECK(book.best_bid()->qty == 6);
  CHECK_FALSE(book.remaining(2).has_value());  // taker は全量約定 → 板にない
}

TEST_CASE("taker remainder rests on its own side") {
  OrderBook book;
  book.add_limit(buy(1, 100, 4));
  auto trades = book.add_limit(sell(2, 100, 10));
  REQUIRE(trades.size() == 1);
  CHECK(trades[0].qty == 4);
  CHECK_FALSE(book.best_bid().has_value());
  REQUIRE(book.best_ask().has_value());
  CHECK(book.best_ask()->price == 100);
  CHECK(book.best_ask()->qty == 6);
  CHECK(book.remaining(2).value_or(-1) == 6);
}

TEST_CASE("price priority: best-priced resting order fills first") {
  OrderBook book;
  book.add_limit(buy(1, 100, 5));
  book.add_limit(buy(2, 101, 5));
  auto trades = book.add_limit(sell(3, 100, 5));
  REQUIRE(trades.size() == 1);
  CHECK(trades[0].maker_id == 2);
  CHECK(trades[0].price == 101);
  CHECK(book.remaining(1).value_or(-1) == 5);
  CHECK(book.best_bid()->price == 100);
}

TEST_CASE("time priority: earlier order at same price fills first") {
  OrderBook book;
  book.add_limit(buy(1, 100, 5));
  book.add_limit(buy(2, 100, 5));
  auto trades = book.add_limit(sell(3, 100, 5));
  REQUIRE(trades.size() == 1);
  CHECK(trades[0].maker_id == 1);
  CHECK_FALSE(book.remaining(1).has_value());
  CHECK(book.remaining(2).value_or(-1) == 5);
}

TEST_CASE("sweep across multiple levels in price order") {
  OrderBook book;
  book.add_limit(sell(1, 101, 3));
  book.add_limit(sell(2, 100, 3));
  book.add_limit(sell(3, 102, 3));
  auto trades = book.add_limit(buy(4, 102, 7));
  REQUIRE(trades.size() == 3);
  CHECK(trades[0].maker_id == 2);
  CHECK(trades[0].price == 100);
  CHECK(trades[0].qty == 3);
  CHECK(trades[1].maker_id == 1);
  CHECK(trades[1].price == 101);
  CHECK(trades[1].qty == 3);
  CHECK(trades[2].maker_id == 3);
  CHECK(trades[2].price == 102);
  CHECK(trades[2].qty == 1);
  CHECK(book.remaining(3).value_or(-1) == 2);
  CHECK(book.best_ask()->price == 102);
  CHECK(book.best_ask()->qty == 2);
  CHECK_FALSE(book.best_bid().has_value());
}

TEST_CASE("cancel removes a resting order") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  book.add_limit(buy(2, 100, 5));
  CHECK(book.cancel(1));
  CHECK_FALSE(book.remaining(1).has_value());
  CHECK(book.best_bid()->qty == 5);  // レベルの合計から 1 の分が消える
  CHECK(book.cancel(2));
  CHECK_FALSE(book.best_bid().has_value());  // 空になったレベルは消える
  CHECK_FALSE(book.cancel(1));    // 二重取消
  CHECK_FALSE(book.cancel(999));  // 存在しない ID
}

TEST_CASE("non-positive qty is rejected without matching or resting") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));

  auto zero = book.add_limit(sell(2, 100, 0));
  CHECK(zero.empty());
  CHECK(book.rejects().non_positive_qty == 1);
  CHECK(book.rejects().duplicate_order_id == 0);
  CHECK(book.remaining(1).value_or(-1) == 10);
  CHECK_FALSE(book.remaining(2).has_value());

  auto neg = book.add_limit(sell(3, 100, -5));
  CHECK(neg.empty());
  CHECK(book.rejects().non_positive_qty == 2);
  CHECK(book.best_bid()->qty == 10);
  CHECK_FALSE(book.remaining(3).has_value());
}

TEST_CASE("duplicate resting OrderId is rejected even if it would cross") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  book.add_limit(sell(2, 101, 5));

  auto trades = book.add_limit(sell(1, 100, 4));  // ID 1 は resting 中
  CHECK(trades.empty());
  CHECK(book.rejects().duplicate_order_id == 1);
  CHECK(book.rejects().non_positive_qty == 0);
  CHECK(book.remaining(1).value_or(-1) == 10);
  CHECK(book.remaining(2).value_or(-1) == 5);
  CHECK(book.best_bid()->qty == 10);
  CHECK(book.best_ask()->qty == 5);
}

TEST_CASE("qty check precedes duplicate: at most one reject counter per call") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));

  auto trades = book.add_limit(buy(1, 99, 0));  // qty 違反かつ ID 重複
  CHECK(trades.empty());
  CHECK(book.rejects().non_positive_qty == 1);
  CHECK(book.rejects().duplicate_order_id == 0);
  CHECK(book.remaining(1).value_or(-1) == 10);
}

TEST_CASE("reject counters accumulate independently across calls") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  book.add_limit(sell(1, 100, 1));   // duplicate
  book.add_limit(buy(2, 100, 0));    // non-positive
  book.add_limit(buy(3, 100, -1));   // non-positive
  book.add_limit(sell(1, 99, 5));    // duplicate again
  CHECK(book.rejects().duplicate_order_id == 2);
  CHECK(book.rejects().non_positive_qty == 2);
  CHECK(book.remaining(1).value_or(-1) == 10);
}

// 未決定事項の現状記録。仕様として確定したものではない。
TEST_CASE("current behavior (undecided): OrderId may be reused after cancel") {
  OrderBook book;
  book.add_limit(buy(1, 100, 10));
  REQUIRE(book.cancel(1));

  auto trades = book.add_limit(buy(1, 101, 7));
  CHECK(trades.empty());
  CHECK(book.rejects().duplicate_order_id == 0);
  CHECK(book.remaining(1).value_or(-1) == 7);
  CHECK(book.best_bid()->price == 101);
  CHECK(book.best_bid()->qty == 7);
}

TEST_CASE("non-monotonic received_at is rejected") {
  OrderBook book;
  book.add_limit(buy_at(1, 100, 10, 1), 1);

  auto trades = book.add_limit(buy_at(2, 100, 5, 2), 0);
  CHECK(trades.empty());
  CHECK(book.rejects().non_monotonic_timestamp == 1);
  CHECK(book.rejects().non_positive_qty == 0);
  CHECK(book.rejects().duplicate_order_id == 0);
  CHECK(book.remaining(1).value_or(-1) == 10);
  CHECK_FALSE(book.remaining(2).has_value());
}

TEST_CASE("decided_at after received_at is rejected") {
  OrderBook book;

  auto trades = book.add_limit(buy_at(1, 100, 10, 10), 5);
  CHECK(trades.empty());
  CHECK(book.rejects().non_monotonic_timestamp == 1);
  CHECK_FALSE(book.best_bid().has_value());
}

TEST_CASE("decided_at reversal is allowed") {
  OrderBook book;
  book.add_limit(buy_at(1, 100, 10, 20), 20);

  auto trades = book.add_limit(buy_at(2, 101, 5, 10), 21);
  CHECK(trades.empty());
  CHECK(book.rejects().non_monotonic_timestamp == 0);
  CHECK(book.remaining(1).value_or(-1) == 10);
  CHECK(book.remaining(2).value_or(-1) == 5);
}

namespace {
void run_sample_sequence(OrderBook& book) {
  book.add_limit(buy(1, 100, 10));
  book.add_limit(sell(2, 100, 4));
  book.add_limit(buy(3, 99, 5));
  book.cancel(1);
  book.add_limit(sell_at(4, 101, 3, 50));
}
}  // namespace

TEST_CASE("locations_ index matches book contents") {
  OrderBook book;
  CHECK(book.locations_consistent());
  book.add_limit(buy(1, 100, 10));
  CHECK(book.locations_consistent());
  book.add_limit(sell(2, 100, 4));
  CHECK(book.locations_consistent());
  book.cancel(1);
  CHECK(book.locations_consistent());
  book.add_limit(sell(3, 101, 5));
  CHECK(book.locations_consistent());
}

TEST_CASE("identical operation sequences yield identical state hashes") {
  OrderBook a;
  OrderBook b;
  run_sample_sequence(a);
  run_sample_sequence(b);
  CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("differing operation sequences yield different state hashes") {
  OrderBook a;
  OrderBook b;
  a.add_limit(buy(1, 100, 10));
  b.add_limit(buy(1, 100, 11));
  CHECK(a.state_hash() != b.state_hash());
}
