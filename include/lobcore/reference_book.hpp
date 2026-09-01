#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include <lobcore/book.hpp>

namespace lobcore {

// Stage 1–2 時点の素朴な板実装のスナップショット。
// 最適化後も削除せず、差分テストの参照として残す。
class ReferenceBook {
 public:
  std::vector<Trade> add_limit(const Order& order, Timestamp received_at);

  bool cancel(OrderId id);

  std::optional<Level> best_bid() const;
  std::optional<Level> best_ask() const;

  std::optional<Qty> remaining(OrderId id) const;

  RejectCounts rejects() const noexcept { return rejects_; }

  std::uint64_t state_hash() const noexcept;
  bool          locations_consistent() const;

 private:
  std::optional<Side> resting_side(OrderId id) const;

  struct RestingOrder {
    OrderId       id;
    Qty           qty;
    std::uint64_t seq;
  };

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
  std::uint64_t                         next_seq_          = 0;
  Timestamp                             last_received_at_  = 0;
  bool                                  has_last_received_ = false;
  RejectCounts                          rejects_{};
};

}  // namespace lobcore
