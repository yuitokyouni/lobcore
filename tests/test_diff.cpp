#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <lobcore/book.hpp>
#include <lobcore/reference_book.hpp>

using namespace lobcore;

namespace {

bool same_level(const std::optional<Level>& a, const std::optional<Level>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (!a.has_value()) {
    return true;
  }
  return a->price == b->price && a->qty == b->qty;
}

bool same_trades(const std::vector<Trade>& a, const std::vector<Trade>& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].maker_id != b[i].maker_id || a[i].taker_id != b[i].taker_id ||
        a[i].price != b[i].price || a[i].qty != b[i].qty) {
      return false;
    }
  }
  return true;
}

void check_books_equal(const OrderBook& live, const ReferenceBook& ref, OrderId probe_id) {
  CHECK(same_level(live.best_bid(), ref.best_bid()));
  CHECK(same_level(live.best_ask(), ref.best_ask()));
  CHECK(live.remaining(probe_id) == ref.remaining(probe_id));
  CHECK(live.state_hash() == ref.state_hash());
  CHECK(live.rejects().duplicate_order_id == ref.rejects().duplicate_order_id);
  CHECK(live.rejects().non_positive_qty == ref.rejects().non_positive_qty);
  CHECK(live.rejects().non_monotonic_timestamp == ref.rejects().non_monotonic_timestamp);
}

// 乱数操作列を両板に流し、返り値と状態が一致することを検証する。
// 部分約定・スイープ・キャンセル・拒否が混ざるよう、価格帯と操作比率を固定する。
void run_differential(std::uint64_t seed, int ops) {
  std::mt19937_64 rng(seed);

  std::uniform_int_distribution<int>       op_kind(0, 99);
  std::uniform_int_distribution<Price>     bid_price(90, 100);
  std::uniform_int_distribution<Price>     ask_price(100, 110);
  std::uniform_int_distribution<Price>     cross_buy(100, 110);
  std::uniform_int_distribution<Price>     cross_sell(90, 100);
  std::uniform_int_distribution<Qty>       qty(1, 20);
  std::bernoulli_distribution              is_buy(0.5);

  OrderBook     live;
  ReferenceBook ref;
  OrderId       next_id = 1;
  Timestamp     next_ts = 1;
  std::vector<OrderId> resting;

  for (int i = 0; i < ops; ++i) {
    const int kind = op_kind(rng);

    if (kind < 10 && !resting.empty()) {
      // cancel
      std::uniform_int_distribution<std::size_t> pick(0, resting.size() - 1);
      const std::size_t idx = pick(rng);
      const OrderId     id  = resting[idx];
      const bool        a   = live.cancel(id);
      const bool        b   = ref.cancel(id);
      CHECK(a == b);
      if (a) {
        resting.erase(resting.begin() + static_cast<std::ptrdiff_t>(idx));
      }
      check_books_equal(live, ref, id);
      continue;
    }

    if (kind < 15) {
      // non-positive qty reject
      const Order order{next_id++, Side::Buy, 100, 0, next_ts};
      const auto  ta = live.add_limit(order, next_ts);
      const auto  tb = ref.add_limit(order, next_ts);
      CHECK(same_trades(ta, tb));
      check_books_equal(live, ref, order.id);
      ++next_ts;
      continue;
    }

    if (kind < 20 && !resting.empty()) {
      // duplicate id reject
      std::uniform_int_distribution<std::size_t> pick(0, resting.size() - 1);
      const OrderId id = resting[pick(rng)];
      const Order   order{id, Side::Sell, 99, 5, next_ts};
      const auto    ta = live.add_limit(order, next_ts);
      const auto    tb = ref.add_limit(order, next_ts);
      CHECK(same_trades(ta, tb));
      check_books_equal(live, ref, id);
      ++next_ts;
      continue;
    }

    // normal / crossing add: mix resting and aggressive orders
    const bool buy = is_buy(rng);
    Price      price = 0;
    Qty        q     = qty(rng);
    if (kind < 55) {
      // non-crossing rest
      price = buy ? bid_price(rng) : ask_price(rng);
      if (buy && price >= 100) {
        price = 99;
      }
      if (!buy && price <= 100) {
        price = 101;
      }
    } else if (kind < 80) {
      // likely partial / full fill at touch
      price = buy ? 100 : 100;
      q     = qty(rng);
    } else {
      // sweep-ish aggressive
      price = buy ? cross_buy(rng) : cross_sell(rng);
      q     = qty(rng) + 10;
    }

    const OrderId id = next_id++;
    const Order   order{id, buy ? Side::Buy : Side::Sell, price, q, next_ts};
    const auto    ta = live.add_limit(order, next_ts);
    const auto    tb = ref.add_limit(order, next_ts);
    CHECK(same_trades(ta, tb));

    // rebuild resting list from remaining probes of recent ids would be costly;
    // track by post-state remaining
    resting.erase(std::remove_if(resting.begin(), resting.end(),
                                 [&](OrderId rid) {
                                   return !live.remaining(rid).has_value();
                                 }),
                  resting.end());
    if (live.remaining(id).has_value()) {
      resting.push_back(id);
    }

    check_books_equal(live, ref, id);
    ++next_ts;
  }

  CHECK(live.state_hash() == ref.state_hash());
}

}  // namespace

TEST_CASE("OrderBook matches ReferenceBook on seeded random streams") {
  run_differential(/*seed=*/42, /*ops=*/2000);
  run_differential(/*seed=*/7, /*ops=*/2000);
  run_differential(/*seed=*/123456789ULL, /*ops=*/2000);
}
