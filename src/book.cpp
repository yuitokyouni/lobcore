#include <lobcore/book.hpp>

#include <algorithm>

namespace lobcore {

Qty OrderBook::level_qty(const std::deque<RestingOrder>& level) {
  Qty total = 0;
  for (const auto& o : level) {
    total += o.qty;
  }
  return total;
}

std::vector<Trade> OrderBook::add_limit(const Order& order) {
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

  std::vector<Trade> trades;
  Qty remaining_qty = order.qty;

  auto match_and_rest = [&](auto& opposite_levels, auto& own_levels, Side own_side,
                            auto stop_crossing) {
    while (remaining_qty > 0 && !opposite_levels.empty()) {
      auto level_it = opposite_levels.begin();
      if (stop_crossing(level_it->first, order.price)) {
        break;
      }
      auto& queue = level_it->second;
      auto& maker = queue.front();
      const Qty fill = std::min(remaining_qty, maker.qty);
      trades.push_back(Trade{.maker_id = maker.id,
                               .taker_id = order.id,
                               .price    = level_it->first,
                               .qty      = fill});
      maker.qty -= fill;
      remaining_qty -= fill;
      if (maker.qty == 0) {
        locations_.erase(maker.id);
        queue.pop_front();
        if (queue.empty()) {
          opposite_levels.erase(level_it);
        }
      }
    }
    if (remaining_qty > 0) {
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

}  // namespace lobcore
