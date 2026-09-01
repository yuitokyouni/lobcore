#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include <lobcore/types.hpp>

namespace lobcore {

// 入力となる注文。qty は「残数量」の初期値。
struct Order {
  OrderId   id;
  Side      side;
  Price     price;
  Qty       qty;
  Timestamp decided_at = 0;  // エージェントが発注を決めた時刻
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

  friend constexpr bool operator==(const Level& a, const Level& b) noexcept {
    return a.price == b.price && a.qty == b.qty;
  }
};

// add_limit が契約違反として拒否した累積件数。
// 検査は qty <= 0 → 重複 ID の順で、1 回の呼び出しで増えるカウンタは高々 1 つ。
// したがって両カウンタの合計は、拒否された注文数と一致しない場合がある
// (qty <= 0 かつ ID 重複の注文は non_positive_qty だけが増える)。
struct RejectCounts {
  std::uint64_t duplicate_order_id       = 0;
  std::uint64_t non_positive_qty         = 0;
  std::uint64_t non_monotonic_timestamp  = 0;
};

// 単一銘柄・価格時間優先 (price-time priority) の板。
//
// 仕様 (tests/test_book.cpp がこの仕様の実行可能な定義):
//   - add_limit: 反対側の板と価格が交差する限り、価格優先 → 同一価格なら到着順で
//     約定させる。残りがあれば自分の側に載せる。発生した約定を到着順で返す。
//   - cancel: 板に載っている注文を取り除く。見つからなければ false。
//   - 到着順 (時間優先の根拠) は add_limit が呼ばれた順で決まる。
//     壁時計は使わない。内部で連番を振ること。
//   - qty <= 0、または板に resting 中の OrderId の再投入は契約違反。
//     約定も resting もせず空の trades を返し、rejects() に記録する。
//
// 未決定事項 (自分で決めてテストを足す):
//   - 同一 ID が cancel 後に再利用されたときの扱い
class LoggedBook;

class OrderBook {
 public:
  // 発生した約定を返す。残数量は板に載る。
  // 契約違反なら空を返し、板は変えない。
  std::vector<Trade> add_limit(const Order& order, Timestamp received_at);

  // 取り除けたら true。見つからなければ false。
  bool cancel(OrderId id);

  std::optional<Level> best_bid() const;
  std::optional<Level> best_ask() const;

  // 板に載っている注文の残数量。載っていなければ nullopt。
  std::optional<Qty> remaining(OrderId id) const;

  RejectCounts rejects() const noexcept { return rejects_; }

  std::uint64_t state_hash() const noexcept;
  bool          locations_consistent() const;

 private:
  friend class LoggedBook;

  std::optional<Side> resting_side(OrderId id) const;
  // 板に載っている注文。seq は到着順 (時間優先) の内部連番。
  struct RestingOrder {
    OrderId       id;
    Qty           qty;
    std::uint64_t seq;
  };

  // OrderId → 価格レベルの位置。レベル内は線形走査する。
  struct Location {
    Side  side;
    Price price;
  };

  using BidLevels = std::map<Price, std::deque<RestingOrder>, std::greater<Price>>;
  using AskLevels = std::map<Price, std::deque<RestingOrder>>;

  static Qty level_qty(const std::deque<RestingOrder>& level);

  BidLevels                             bids_;
  AskLevels                             asks_;
  std::unordered_map<OrderId, Location> locations_;
  std::uint64_t                         next_seq_           = 0;
  Timestamp                             last_received_at_   = 0;
  bool                                  has_last_received_  = false;
  RejectCounts                          rejects_{};
};

}  // namespace lobcore
