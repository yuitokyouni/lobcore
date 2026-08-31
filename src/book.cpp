#include <lobcore/book.hpp>

namespace lobcore {

// TODO(Stage 1): 全部書き換える。今はリンクを通すためのスタブ。

std::vector<Trade> OrderBook::add_limit(const Order& /*order*/) {
  return {};
}

bool OrderBook::cancel(OrderId /*id*/) {
  return false;
}

std::optional<Level> OrderBook::best_bid() const {
  return std::nullopt;
}

std::optional<Level> OrderBook::best_ask() const {
  return std::nullopt;
}

std::optional<Qty> OrderBook::remaining(OrderId /*id*/) const {
  return std::nullopt;
}

}  // namespace lobcore
