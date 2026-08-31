#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

#include <lobcore/types.hpp>

namespace lobcore {

// 入力となる注文。qty は「残数量」の初期値。
struct Order {
  OrderId id;
  Side    side;
  Price   price;
  Qty     qty;
};

// 約定 1 件。maker = 板に載っていた側、taker = 今来た側。
// price は必ず maker の指値 (taker の指値ではない)。
struct Trade {
  OrderId maker_id;
  OrderId taker_id;
  Price   price;
  Qty     qty;
};

// 価格レベルの要約 (best_bid / best_ask の戻り値)。
struct Level {
  Price price;
  Qty   qty;  // そのレベルに載っている残数量の合計
};

// 単一銘柄・価格時間優先 (price-time priority) の板。
//
// 仕様 (tests/test_book.cpp がこの仕様の実行可能な定義):
//   - add_limit: 反対側の板と価格が交差する限り、価格優先 → 同一価格なら到着順で
//     約定させる。残りがあれば自分の側に載せる。発生した約定を到着順で返す。
//   - cancel: 板に載っている注文を取り除く。見つからなければ false。
//   - 到着順 (時間優先の根拠) は add_limit が呼ばれた順で決まる。
//     壁時計は使わない。内部で連番を振ること。
//
// 未決定事項 (自分で決めてテストを足す):
//   - 重複した OrderId を add_limit に渡されたときの扱い
//   - qty <= 0 の注文の扱い
//   - 同一 ID が cancel 後に再利用されたときの扱い
class OrderBook {
 public:
  // 発生した約定を返す。残数量は板に載る。
  std::vector<Trade> add_limit(const Order& order);

  // 取り除けたら true。見つからなければ false。
  bool cancel(OrderId id);

  std::optional<Level> best_bid() const;
  std::optional<Level> best_ask() const;

  // 板に載っている注文の残数量。載っていなければ nullopt。
  std::optional<Qty> remaining(OrderId id) const;

 private:
  // 板に載っている注文。seq は到着順 (時間優先) の内部連番。
  struct RestingOrder {
    OrderId       id;
    Qty           qty;
    std::uint64_t seq;
  };

  using BidLevels = std::map<Price, std::deque<RestingOrder>, std::greater<Price>>;
  using AskLevels = std::map<Price, std::deque<RestingOrder>>;

  static Qty level_qty(const std::deque<RestingOrder>& level);

  BidLevels     bids_;
  AskLevels     asks_;
  std::uint64_t next_seq_ = 0;
};

}  // namespace lobcore
