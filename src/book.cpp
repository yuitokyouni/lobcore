#include <lobcore/book.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lobcore {

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime        = 1099511628211ULL;

std::uint64_t fnv1a_byte(std::uint64_t hash, std::uint8_t byte) {
  hash ^= static_cast<std::uint64_t>(byte);
  return hash * kFnvPrime;
}

std::uint64_t fnv1a_u64(std::uint64_t hash, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    hash = fnv1a_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
  return hash;
}

std::uint64_t fnv1a_i64(std::uint64_t hash, std::int64_t value) {
  return fnv1a_u64(hash, static_cast<std::uint64_t>(value));
}

}  // namespace

Qty OrderBook::level_qty(const std::deque<RestingOrder>& level) {
  Qty total = 0;
  for (const auto& o : level) {
    total += o.qty;
  }
  return total;
}

std::vector<LevelMaker> OrderBook::makers_from_level(const std::deque<RestingOrder>& level) {
  std::vector<LevelMaker> makers;
  makers.reserve(level.size());
  for (const auto& order : level) {
    makers.push_back(LevelMaker{order.id, order.qty, order.seq});
  }
  return makers;
}

void OrderBook::apply_level_allocation(std::deque<RestingOrder>& queue,
                                       const AllocateResult&     allocation,
                                       const Price               price,
                                       const OrderId             taker_id,
                                       std::vector<Trade>&       trades,
                                       std::unordered_map<OrderId, Location>& locations) {
  for (const LevelFill& fill : allocation.fills) {
    trades.push_back(Trade{.maker_id = fill.maker_id,
                           .taker_id = taker_id,
                           .price    = price,
                           .qty      = fill.qty});
    for (auto& maker : queue) {
      if (maker.id == fill.maker_id) {
        maker.qty -= fill.qty;
        break;
      }
    }
  }

  for (auto it = queue.begin(); it != queue.end();) {
    if (it->qty == 0) {
      locations.erase(it->id);
      it = queue.erase(it);
    } else {
      ++it;
    }
  }
}

OrderBook::OrderBook() : rule_(std::make_shared<PriceTimePriority>()) {}

OrderBook::OrderBook(std::unique_ptr<AllocationRule> rule)
    : rule_(rule ? std::shared_ptr<AllocationRule>(std::move(rule))
                 : std::make_shared<PriceTimePriority>()) {}

std::vector<Trade> OrderBook::add_limit(const Order& order, Timestamp received_at) {
  // 契約違反: 高々 1 カウンタだけ増やす (qty を先に見る)。
  // 両カウンタの合計 ≠ 拒否注文数になり得る点に注意。
  if (order.qty <= 0) {
    ++rejects_.non_positive_qty;
    return {};
  }
  if (locations_.find(order.id) != locations_.end()) {
    ++rejects_.duplicate_order_id;
    return {};
  }
  if (has_last_received_ && received_at < last_received_at_) {
    ++rejects_.non_monotonic_timestamp;
    return {};
  }
  if (order.decided_at > received_at) {
    ++rejects_.non_monotonic_timestamp;
    return {};
  }

  const OrderBook checkpoint(*this);

  has_last_received_ = true;
  last_received_at_  = received_at;

  std::vector<Trade> trades;
  Qty                remaining_qty = order.qty;
  bool               overflow    = false;

  auto match_and_rest = [&](auto& opposite_levels, auto& own_levels, Side own_side,
                            auto stop_crossing) {
    while (remaining_qty > 0 && !opposite_levels.empty()) {
      auto level_it = opposite_levels.begin();
      if (stop_crossing(level_it->first, order.price)) {
        break;
      }

      auto&       queue       = level_it->second;
      const Price price       = level_it->first;
      const Qty   level_total = level_qty(queue);
      const Qty   take        = std::min(remaining_qty, level_total);
      const auto  makers      = makers_from_level(queue);

      const AllocateResult allocation =
          rule_->allocate_level(makers.data(), makers.size(), take);
      if (allocation.status == AllocateStatus::Overflow) {
        overflow = true;
        return;
      }

      apply_level_allocation(queue, allocation, price, order.id, trades, locations_);
      remaining_qty -= take;
      if (queue.empty()) {
        opposite_levels.erase(level_it);
      }
    }

    if (!overflow && remaining_qty > 0) {
      own_levels[order.price].push_back(
          RestingOrder{.id = order.id, .qty = remaining_qty, .seq = next_seq_++});
      locations_[order.id] = Location{own_side, order.price};
    }
  };

  if (order.side == Side::Buy) {
    match_and_rest(asks_, bids_, Side::Buy, std::greater<>{});
  } else {
    match_and_rest(bids_, asks_, Side::Sell, std::less<>{});
  }

  if (overflow) {
    bids_              = checkpoint.bids_;
    asks_              = checkpoint.asks_;
    locations_         = checkpoint.locations_;
    has_last_received_ = checkpoint.has_last_received_;
    last_received_at_  = checkpoint.last_received_at_;
    ++rejects_.allocation_overflow;
    return {};
  }

  return trades;
}

bool OrderBook::cancel(OrderId id) {
  const auto loc_it = locations_.find(id);
  if (loc_it == locations_.end()) {
    return false;
  }
  const Location loc = loc_it->second;

  auto erase_from = [&](auto& levels) {
    auto level_it = levels.find(loc.price);
    if (level_it == levels.end()) {
      return false;
    }
    auto& queue = level_it->second;
    for (auto it = queue.begin(); it != queue.end(); ++it) {
      if (it->id == id) {
        queue.erase(it);
        if (queue.empty()) {
          levels.erase(level_it);
        }
        locations_.erase(loc_it);
        return true;
      }
    }
    return false;
  };

  if (loc.side == Side::Buy) {
    return erase_from(bids_);
  }
  return erase_from(asks_);
}

std::optional<Level> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  const auto& [price, level] = *bids_.begin();
  return Level{price, level_qty(level)};
}

std::optional<Level> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  const auto& [price, level] = *asks_.begin();
  return Level{price, level_qty(level)};
}

std::optional<Qty> OrderBook::remaining(OrderId id) const {
  const auto loc_it = locations_.find(id);
  if (loc_it == locations_.end()) {
    return std::nullopt;
  }
  const Location& loc = loc_it->second;

  const auto* queue = [&]() -> const std::deque<RestingOrder>* {
    if (loc.side == Side::Buy) {
      const auto level_it = bids_.find(loc.price);
      if (level_it == bids_.end()) {
        return nullptr;
      }
      return &level_it->second;
    }
    const auto level_it = asks_.find(loc.price);
    if (level_it == asks_.end()) {
      return nullptr;
    }
    return &level_it->second;
  }();

  if (queue == nullptr) {
    return std::nullopt;
  }
  for (const auto& o : *queue) {
    if (o.id == id) {
      return o.qty;
    }
  }
  return std::nullopt;
}

std::optional<Side> OrderBook::resting_side(OrderId id) const {
  const auto loc_it = locations_.find(id);
  if (loc_it == locations_.end()) {
    return std::nullopt;
  }
  return loc_it->second.side;
}

std::uint64_t OrderBook::state_hash() const noexcept {
  std::uint64_t hash = kFnvOffsetBasis;

  for (const auto& [price, level] : bids_) {
    hash = fnv1a_i64(hash, price);
    for (const auto& order : level) {
      hash = fnv1a_u64(hash, order.id);
      hash = fnv1a_i64(hash, order.qty);
      hash = fnv1a_u64(hash, order.seq);
    }
  }

  for (const auto& [price, level] : asks_) {
    hash = fnv1a_i64(hash, price);
    for (const auto& order : level) {
      hash = fnv1a_u64(hash, order.id);
      hash = fnv1a_i64(hash, order.qty);
      hash = fnv1a_u64(hash, order.seq);
    }
  }

  hash = fnv1a_u64(hash, next_seq_);
  hash = fnv1a_u64(hash, rejects_.duplicate_order_id);
  hash = fnv1a_u64(hash, rejects_.non_positive_qty);
  hash = fnv1a_u64(hash, rejects_.non_monotonic_timestamp);
  hash = fnv1a_u64(hash, rejects_.allocation_overflow);
  return hash;
}

bool OrderBook::locations_consistent() const {
  std::unordered_map<OrderId, Location> rebuilt;
  rebuilt.reserve(locations_.size());

  auto index_side = [&](const auto& levels, Side side) -> bool {
    for (const auto& [price, level] : levels) {
      for (const auto& order : level) {
        const Location loc{side, price};
        if (!rebuilt.emplace(order.id, loc).second) {
          return false;
        }
      }
    }
    return true;
  };

  if (!index_side(bids_, Side::Buy)) {
    return false;
  }
  if (!index_side(asks_, Side::Sell)) {
    return false;
  }
  if (rebuilt.size() != locations_.size()) {
    return false;
  }
  for (const auto& [id, loc] : locations_) {
    const auto it = rebuilt.find(id);
    if (it == rebuilt.end() || it->second.side != loc.side || it->second.price != loc.price) {
      return false;
    }
  }
  return true;
}

}  // namespace lobcore
