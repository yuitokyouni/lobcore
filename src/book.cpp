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

  if (order.side == Side::Buy) {
    while (remaining_qty > 0 && !asks_.empty()) {
      auto level_it = asks_.begin();
      if (level_it->first > order.price) {
        break;
      }
      auto& queue = level_it->second;
      auto& maker = queue.front();
      const Qty fill = std::min(remaining_qty, maker.qty);
      trades.push_back(Trade{maker.id, order.id, level_it->first, fill});
      maker.qty -= fill;
      remaining_qty -= fill;
      if (maker.qty == 0) {
        locations_.erase(maker.id);
        queue.pop_front();
        if (queue.empty()) {
          asks_.erase(level_it);
        }
      }
    }
    if (remaining_qty > 0) {
      bids_[order.price].push_back(
          RestingOrder{order.id, remaining_qty, next_seq_++});
      locations_[order.id] = Location{Side::Buy, order.price};
    }
  } else {
    while (remaining_qty > 0 && !bids_.empty()) {
      auto level_it = bids_.begin();
      if (level_it->first < order.price) {
        break;
      }
      auto& queue = level_it->second;
      auto& maker = queue.front();
      const Qty fill = std::min(remaining_qty, maker.qty);
      trades.push_back(Trade{maker.id, order.id, level_it->first, fill});
      maker.qty -= fill;
      remaining_qty -= fill;
      if (maker.qty == 0) {
        locations_.erase(maker.id);
        queue.pop_front();
        if (queue.empty()) {
          bids_.erase(level_it);
        }
      }
    }
    if (remaining_qty > 0) {
      asks_[order.price].push_back(
          RestingOrder{order.id, remaining_qty, next_seq_++});
      locations_[order.id] = Location{Side::Sell, order.price};
    }
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
