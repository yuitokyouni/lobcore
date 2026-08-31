// Stage 1 の仕様書。全部通ったら Stage 1 完了。
#include <catch2/catch_test_macros.hpp>

#include <lobcore/book.hpp>

using namespace lobcore;

namespace {
Order buy(OrderId id, Price p, Qty q) { return Order{id, Side::Buy, p, q}; }
Order sell(OrderId id, Price p, Qty q) { return Order{id, Side::Sell, p, q}; }
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
