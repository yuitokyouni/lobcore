#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <lobcore/types.hpp>

namespace lobcore::detail {

// 価格レベル配列 + レベル内 FIFO（連続スロット + free list）+ 占有ビットマップ。
class DenseBookSide {
 public:
  explicit DenseBookSide(bool best_is_highest_price) : best_is_highest_price_(best_is_highest_price) {}

  struct RestingOrder {
    OrderId       id;
    Qty           qty;
    std::uint64_t seq;
  };

  bool empty() const noexcept { return level_count_ == 0; }

  std::optional<Price> best_price() const;

  void add_resting(OrderId id, Price price, Qty qty, std::uint64_t seq);
  bool cancel_at(OrderId id, Price price);

  Qty level_qty_at(Price price) const;
  std::vector<RestingOrder> makers_at(Price price) const;

  void apply_fills(Price price, const std::vector<std::pair<OrderId, Qty>>& fills);

  template <typename Fn>
  void for_each_level(Fn&& fn) const;

  bool level_nonempty(Price price) const;

  DenseBookSide(const DenseBookSide&)            = default;
  DenseBookSide& operator=(const DenseBookSide&) = default;
  DenseBookSide(DenseBookSide&&)                 = default;
  DenseBookSide& operator=(DenseBookSide&&)      = default;

 private:
  static constexpr std::uint32_t kNull = UINT32_MAX;

  struct Node {
    OrderId       id   = 0;
    Qty           qty  = 0;
    std::uint64_t seq  = 0;
    std::uint32_t prev = kNull;
    std::uint32_t next = kNull;
  };

  struct Level {
    std::uint32_t head      = kNull;
    std::uint32_t tail      = kNull;
    Qty           total_qty = 0;
  };

  void grow_to_include(Price price);
  void set_bit(std::size_t index);
  void clear_bit(std::size_t index);
  bool bit_set(std::size_t index) const;

  std::size_t price_index(Price price) const noexcept {
    return static_cast<std::size_t>(price - min_price_);
  }

  std::uint32_t alloc_node();
  void          free_node(std::uint32_t index);
  void          unlink_node(Price price, std::uint32_t index);
  void          rebuild_bitmap();

  bool                     best_is_highest_price_ = false;
  Price                    min_price_               = 0;
  Price                    max_price_               = -1;
  std::size_t              level_count_             = 0;
  std::vector<Level>       levels_;
  std::vector<std::uint64_t> bitmap_;
  std::vector<Node>        pool_;
  std::vector<std::uint32_t> free_nodes_;
};

template <typename Fn>
void DenseBookSide::for_each_level(Fn&& fn) const {
  if (level_count_ == 0) {
    return;
  }

  auto highest_bit = [](std::uint64_t word) -> int {
    if (word == 0) {
      return -1;
    }
    return 63 - __builtin_clzll(word);
  };
  auto lowest_bit = [](std::uint64_t word) -> int {
    if (word == 0) {
      return -1;
    }
    return __builtin_ctzll(word);
  };

  if (best_is_highest_price_) {
    for (std::size_t w = bitmap_.size(); w-- > 0;) {
      std::uint64_t word = bitmap_[w];
      while (word != 0) {
        const int         bit = highest_bit(word);
        const std::size_t idx = w * 64 + static_cast<std::size_t>(bit);
        if (idx < levels_.size()) {
          fn(min_price_ + static_cast<Price>(idx),
             makers_at(min_price_ + static_cast<Price>(idx)));
        }
        word &= ~(1ULL << bit);
      }
    }
    return;
  }

  for (std::size_t w = 0; w < bitmap_.size(); ++w) {
    std::uint64_t word = bitmap_[w];
    while (word != 0) {
      const int         bit = lowest_bit(word);
      const std::size_t idx = w * 64 + static_cast<std::size_t>(bit);
      if (idx < levels_.size()) {
        fn(min_price_ + static_cast<Price>(idx),
           makers_at(min_price_ + static_cast<Price>(idx)));
      }
      word &= ~(1ULL << bit);
    }
  }
}

}  // namespace lobcore::detail
