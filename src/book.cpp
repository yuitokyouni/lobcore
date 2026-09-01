#include <lobcore/book.hpp>

#include <algorithm>
#include <cstdint>
#include <utility>
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

std::vector<LevelMaker> makers_from_level(
    const std::vector<detail::DenseBookSide::RestingOrder>& level) {
  std::vector<LevelMaker> makers;
  makers.reserve(level.size());
  for (const auto& order : level) {
    makers.push_back(LevelMaker{order.id, order.qty, order.seq});
  }
  return makers;
}

}  // namespace

OrderBook::OrderBook() : bids_(true), asks_(false), rule_(std::make_shared<PriceTimePriority>()) {}

OrderBook::OrderBook(std::unique_ptr<AllocationRule> rule)
    : bids_(true),
      asks_(false),
      rule_(rule ? std::shared_ptr<AllocationRule>(std::move(rule))
                 : std::make_shared<PriceTimePriority>()) {}

std::vector<Trade> OrderBook::add_limit(const Order& order, Timestamp received_at) {
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

  auto match_and_rest = [&](detail::DenseBookSide& opposite, detail::DenseBookSide& own,
                            Side own_side, auto stop_crossing) {
    while (remaining_qty > 0) {
      const auto best = opposite.best_price();
      if (!best || stop_crossing(*best, order.price)) {
        break;
      }

      const Price price       = *best;
      const auto  makers_vec  = opposite.makers_at(price);
      const Qty   level_total = opposite.level_qty_at(price);
      const Qty   take        = std::min(remaining_qty, level_total);
      const auto  makers      = makers_from_level(makers_vec);

      const AllocateResult allocation =
          rule_->allocate_level(makers.data(), makers.size(), take);
      if (allocation.status == AllocateStatus::Overflow) {
        overflow = true;
        return;
      }

      std::vector<std::pair<OrderId, Qty>> fill_pairs;
      fill_pairs.reserve(allocation.fills.size());
      for (const LevelFill& fill : allocation.fills) {
        trades.push_back(Trade{.maker_id = fill.maker_id,
                               .taker_id = order.id,
                               .price    = price,
                               .qty      = fill.qty});
        fill_pairs.emplace_back(fill.maker_id, fill.qty);
        for (const auto& maker : makers_vec) {
          if (maker.id == fill.maker_id && maker.qty == fill.qty) {
            locations_.erase(fill.maker_id);
            break;
          }
        }
      }
      opposite.apply_fills(price, fill_pairs);
      remaining_qty -= take;
    }

    if (!overflow && remaining_qty > 0) {
      own.add_resting(order.id, order.price, remaining_qty, next_seq_++);
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

  const bool removed = loc.side == Side::Buy ? bids_.cancel_at(id, loc.price)
                                             : asks_.cancel_at(id, loc.price);
  if (removed) {
    locations_.erase(loc_it);
  }
  return removed;
}

std::optional<Level> OrderBook::best_bid() const {
  const auto price = bids_.best_price();
  if (!price) {
    return std::nullopt;
  }
  return Level{*price, bids_.level_qty_at(*price)};
}

std::optional<Level> OrderBook::best_ask() const {
  const auto price = asks_.best_price();
  if (!price) {
    return std::nullopt;
  }
  return Level{*price, asks_.level_qty_at(*price)};
}

std::optional<Qty> OrderBook::remaining(OrderId id) const {
  const auto loc_it = locations_.find(id);
  if (loc_it == locations_.end()) {
    return std::nullopt;
  }
  const Location& loc = loc_it->second;

  const auto& side = loc.side == Side::Buy ? bids_ : asks_;
  if (!side.level_nonempty(loc.price)) {
    return std::nullopt;
  }
  for (const auto& order : side.makers_at(loc.price)) {
    if (order.id == id) {
      return order.qty;
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

  bids_.for_each_level([&](Price price, const std::vector<detail::DenseBookSide::RestingOrder>& level) {
    hash = fnv1a_i64(hash, price);
    for (const auto& order : level) {
      hash = fnv1a_u64(hash, order.id);
      hash = fnv1a_i64(hash, order.qty);
      hash = fnv1a_u64(hash, order.seq);
    }
  });

  asks_.for_each_level([&](Price price, const std::vector<detail::DenseBookSide::RestingOrder>& level) {
    hash = fnv1a_i64(hash, price);
    for (const auto& order : level) {
      hash = fnv1a_u64(hash, order.id);
      hash = fnv1a_i64(hash, order.qty);
      hash = fnv1a_u64(hash, order.seq);
    }
  });

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

  auto index_side = [&](const detail::DenseBookSide& side, Side book_side) -> bool {
    bool ok = true;
    side.for_each_level([&](Price price, const std::vector<detail::DenseBookSide::RestingOrder>& level) {
      for (const auto& order : level) {
        const Location loc{book_side, price};
        if (!rebuilt.emplace(order.id, loc).second) {
          ok = false;
        }
      }
    });
    return ok;
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
