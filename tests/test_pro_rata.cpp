// Pro-rata 配分規則の仕様書 (案 A: 最大剰余法、rem 同点は seq 昇順)。
#include <catch2/catch_test_macros.hpp>

#include <lobcore/book.hpp>
#include <lobcore/kernel/allocation_rule.hpp>

#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

using namespace lobcore;

namespace {

Timestamp next_ts() {
  static Timestamp t = 0;
  return ++t;
}

Order buy_at(OrderId id, Price price, Qty qty) {
  return Order{id, Side::Buy, price, qty, next_ts()};
}

Order sell_at(OrderId id, Price price, Qty qty) {
  return Order{id, Side::Sell, price, qty, next_ts()};
}

OrderBook make_pro_rata_book() {
  return OrderBook(std::make_unique<ProRata>());
}

void check_fills(const std::vector<Trade>& trades,
                 OrderId                   taker_id,
                 Price                     price,
                 std::initializer_list<std::pair<OrderId, Qty>> expected) {
  std::unordered_map<OrderId, Qty> got;
  for (const auto& trade : trades) {
    if (trade.taker_id == taker_id && trade.price == price) {
      got[trade.maker_id] += trade.qty;
    }
  }
  REQUIRE(got.size() == expected.size());
  for (const auto& [maker_id, qty] : expected) {
    INFO("maker_id=" << maker_id);
    const auto it = got.find(maker_id);
    REQUIRE(it != got.end());
    CHECK(it->second == qty);
    for (const auto& trade : trades) {
      if (trade.maker_id == maker_id && trade.taker_id == taker_id) {
        CHECK(trade.price == price);
      }
    }
  }
}

void check_maker_order(const std::vector<Trade>& trades, std::initializer_list<OrderId> maker_order) {
  std::vector<OrderId> got;
  got.reserve(trades.size());
  for (const auto& trade : trades) {
    got.push_back(trade.maker_id);
  }
  REQUIRE(got.size() == maker_order.size());
  std::size_t i = 0;
  for (const OrderId id : maker_order) {
    CHECK(got[i] == id);
    ++i;
  }
}

}  // namespace

TEST_CASE("pro-rata splits evenly across equal resting quantities") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 5), 1);
  book.add_limit(sell_at(2, 100, 5), 2);

  const auto trades = book.add_limit(buy_at(3, 100, 10), 3);

  REQUIRE(trades.size() == 2);
  check_fills(trades, 3, 100, {{1, 5}, {2, 5}});
  check_maker_order(trades, {1, 2});
  CHECK_FALSE(book.remaining(1).has_value());
  CHECK_FALSE(book.remaining(2).has_value());
}

TEST_CASE("pro-rata allocates in exact proportion when T divides level cleanly") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 3), 1);
  book.add_limit(sell_at(2, 100, 7), 2);

  const auto trades = book.add_limit(buy_at(3, 100, 10), 3);

  REQUIRE(trades.size() == 2);
  check_fills(trades, 3, 100, {{1, 3}, {2, 7}});
}

TEST_CASE("pro-rata gives remainder lot to largest T*qi remainder") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 3), 1);
  book.add_limit(sell_at(2, 100, 7), 2);

  const auto trades = book.add_limit(buy_at(3, 100, 7), 3);

  REQUIRE(trades.size() == 2);
  check_fills(trades, 3, 100, {{1, 2}, {2, 5}});
}

TEST_CASE("pro-rata with T=1 awards single lot to highest remainder maker") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 3), 1);
  book.add_limit(sell_at(2, 100, 7), 2);

  const auto trades = book.add_limit(buy_at(3, 100, 1), 3);

  REQUIRE(trades.size() == 1);
  check_fills(trades, 3, 100, {{2, 1}});
  CHECK(book.remaining(1).value_or(-1) == 3);
  CHECK(book.remaining(2).value_or(-1) == 6);
}

TEST_CASE("pro-rata breaks equal remainders by seq ascending") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 5), 1);
  book.add_limit(sell_at(2, 100, 5), 2);

  const auto trades = book.add_limit(buy_at(3, 100, 1), 3);

  REQUIRE(trades.size() == 1);
  check_fills(trades, 3, 100, {{1, 1}});
  CHECK(book.remaining(1).value_or(-1) == 4);
  CHECK(book.remaining(2).value_or(-1) == 5);
}

TEST_CASE("pro-rata with single maker at level matches min of T and qi") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 10), 1);

  const auto trades = book.add_limit(buy_at(2, 100, 5), 2);

  REQUIRE(trades.size() == 1);
  check_fills(trades, 2, 100, {{1, 5}});
  CHECK(book.remaining(1).value_or(-1) == 5);
}

TEST_CASE("pro-rata uses resting quantities after partial fills on same level") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 100, 10), 1);
  book.add_limit(sell_at(2, 100, 20), 2);

  const auto first = book.add_limit(buy_at(3, 100, 15), 3);
  check_fills(first, 3, 100, {{1, 5}, {2, 10}});
  CHECK(book.remaining(1).value_or(-1) == 5);
  CHECK(book.remaining(2).value_or(-1) == 10);

  const auto second = book.add_limit(buy_at(4, 100, 10), 4);
  REQUIRE(second.size() == 2);
  check_fills(second, 4, 100, {{1, 3}, {2, 7}});
}

TEST_CASE("pro-rata emits trades in seq ascending order within a level") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(10, 100, 2), 1);
  book.add_limit(sell_at(20, 100, 2), 2);
  book.add_limit(sell_at(30, 100, 2), 3);

  const auto trades = book.add_limit(buy_at(40, 100, 4), 4);

  REQUIRE(trades.size() == 3);
  check_maker_order(trades, {10, 20, 30});
}

TEST_CASE("pro-rata keeps price priority across levels") {
  OrderBook book = make_pro_rata_book();
  book.add_limit(sell_at(1, 99, 5), 1);
  book.add_limit(sell_at(2, 100, 3), 2);
  book.add_limit(sell_at(3, 100, 7), 3);

  const auto trades = book.add_limit(buy_at(4, 100, 12), 4);

  REQUIRE(trades.size() == 3);
  check_fills(trades, 4, 99, {{1, 5}});
  check_fills(trades, 4, 100, {{2, 2}, {3, 5}});
  check_maker_order(trades, {1, 2, 3});
}

TEST_CASE("pro-rata rejects when T times resting qty would overflow int64") {
  OrderBook book = make_pro_rata_book();
  const Qty large = std::numeric_limits<Qty>::max() / 2 + 1;
  book.add_limit(sell_at(1, 100, large), 1);

  const auto trades = book.add_limit(buy_at(2, 100, 2), 2);

  CHECK(trades.empty());
  CHECK(book.rejects().allocation_overflow == 1);
  CHECK(book.remaining(1).value_or(-1) == large);
  CHECK_FALSE(book.remaining(2).has_value());
}
