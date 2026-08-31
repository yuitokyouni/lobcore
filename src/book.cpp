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
        queue.pop_front();
        if (queue.empty()) {
          asks_.erase(level_it);
        }
      }
    }
    if (remaining_qty > 0) {
      bids_[order.price].push_back(
          RestingOrder{order.id, remaining_qty, next_seq_++});
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
        queue.pop_front();
        if (queue.empty()) {
          bids_.erase(level_it);
        }
      }
    }
    if (remaining_qty > 0) {
      asks_[order.price].push_back(
          RestingOrder{order.id, remaining_qty, next_seq_++});
    }
  }

  return trades;
}

bool OrderBook::cancel(OrderId /*id*/) {
  return false;
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
  for (const auto& entry : bids_) {
    for (const auto& o : entry.second) {
      if (o.id == id) {
        return o.qty;
      }
    }
  }
  for (const auto& entry : asks_) {
    for (const auto& o : entry.second) {
      if (o.id == id) {
        return o.qty;
      }
    }
  }
  return std::nullopt;
}

}  // namespace lobcore
