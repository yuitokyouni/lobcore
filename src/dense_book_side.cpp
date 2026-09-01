#include <lobcore/detail/dense_book_side.hpp>

#include <algorithm>
#include <utility>

namespace lobcore::detail {

namespace {

std::size_t bitmap_words(std::size_t n_levels) {
  return (n_levels + 63) / 64;
}

int highest_set_bit_in_word(std::uint64_t word) {
  if (word == 0) {
    return -1;
  }
  return 63 - __builtin_clzll(word);
}

int lowest_set_bit_in_word(std::uint64_t word) {
  if (word == 0) {
    return -1;
  }
  return __builtin_ctzll(word);
}

}  // namespace

void DenseBookSide::grow_to_include(const Price price) {
  if (level_count_ == 0) {
    min_price_   = price;
    max_price_   = price;
    level_count_ = 1;
    levels_.assign(1, Level{});
    bitmap_.assign(1, 0);
    return;
  }

  if (price >= min_price_ && price <= max_price_) {
    return;
  }

  if (price < min_price_) {
    const std::size_t add = static_cast<std::size_t>(min_price_ - price);
    levels_.insert(levels_.begin(), add, Level{});
    min_price_ = price;
  } else {
    const std::size_t new_count = static_cast<std::size_t>(price - min_price_) + 1;
    levels_.resize(new_count);
    max_price_ = price;
  }
  level_count_ = levels_.size();
  rebuild_bitmap();
}

void DenseBookSide::rebuild_bitmap() {
  bitmap_.assign(bitmap_words(levels_.size()), 0);
  for (std::size_t i = 0; i < levels_.size(); ++i) {
    if (levels_[i].head != kNull) {
      set_bit(i);
    }
  }
}

void DenseBookSide::set_bit(const std::size_t index) {
  bitmap_[index / 64] |= (1ULL << (index % 64));
}

void DenseBookSide::clear_bit(const std::size_t index) {
  bitmap_[index / 64] &= ~(1ULL << (index % 64));
}

bool DenseBookSide::bit_set(const std::size_t index) const {
  return (bitmap_[index / 64] & (1ULL << (index % 64))) != 0;
}

std::uint32_t DenseBookSide::alloc_node() {
  if (!free_nodes_.empty()) {
    const std::uint32_t index = free_nodes_.back();
    free_nodes_.pop_back();
    pool_[index] = Node{};
    return index;
  }
  const std::uint32_t index = static_cast<std::uint32_t>(pool_.size());
  pool_.push_back(Node{});
  return index;
}

void DenseBookSide::free_node(const std::uint32_t index) {
  pool_[index] = Node{};
  free_nodes_.push_back(index);
}

void DenseBookSide::unlink_node(const Price price, const std::uint32_t index) {
  const std::size_t li = price_index(price);
  Level&            lv = levels_[li];
  Node&             node = pool_[index];

  if (node.prev != kNull) {
    pool_[node.prev].next = node.next;
  } else {
    lv.head = node.next;
  }
  if (node.next != kNull) {
    pool_[node.next].prev = node.prev;
  } else {
    lv.tail = node.prev;
  }

  lv.total_qty -= node.qty;
  free_node(index);

  if (lv.head == kNull) {
    clear_bit(li);
  }
}

std::optional<Price> DenseBookSide::best_price() const {
  if (level_count_ == 0) {
    return std::nullopt;
  }

  if (best_is_highest_price_) {
    for (std::size_t w = bitmap_.size(); w-- > 0;) {
      const int bit = highest_set_bit_in_word(bitmap_[w]);
      if (bit >= 0) {
        return min_price_ + static_cast<Price>(w * 64 + static_cast<std::size_t>(bit));
      }
    }
    return std::nullopt;
  }

  for (std::size_t w = 0; w < bitmap_.size(); ++w) {
    const int bit = lowest_set_bit_in_word(bitmap_[w]);
    if (bit >= 0) {
      return min_price_ + static_cast<Price>(w * 64 + static_cast<std::size_t>(bit));
    }
  }
  return std::nullopt;
}

void DenseBookSide::add_resting(const OrderId id, const Price price, const Qty qty,
                                const std::uint64_t seq) {
  grow_to_include(price);
  const std::size_t li = price_index(price);
  Level&            lv = levels_[li];

  const std::uint32_t index = alloc_node();
  Node&               node  = pool_[index];
  node.id                   = id;
  node.qty                  = qty;
  node.seq                  = seq;

  if (lv.head == kNull) {
    lv.head = lv.tail = index;
    set_bit(li);
  } else {
    pool_[lv.tail].next = index;
    node.prev           = lv.tail;
    lv.tail             = index;
  }
  lv.total_qty += qty;
}

bool DenseBookSide::cancel_at(const OrderId id, const Price price) {
  if (level_count_ == 0 || price < min_price_ || price > max_price_) {
    return false;
  }
  const std::size_t li = price_index(price);
  if (!bit_set(li)) {
    return false;
  }

  for (std::uint32_t cur = levels_[li].head; cur != kNull; cur = pool_[cur].next) {
    if (pool_[cur].id == id) {
      unlink_node(price, cur);
      return true;
    }
  }
  return false;
}

Qty DenseBookSide::level_qty_at(const Price price) const {
  if (level_count_ == 0 || price < min_price_ || price > max_price_) {
    return 0;
  }
  const std::size_t li = price_index(price);
  if (!bit_set(li)) {
    return 0;
  }
  return levels_[li].total_qty;
}

std::vector<DenseBookSide::RestingOrder> DenseBookSide::makers_at(const Price price) const {
  std::vector<RestingOrder> out;
  if (level_count_ == 0 || price < min_price_ || price > max_price_) {
    return out;
  }
  const std::size_t li = price_index(price);
  if (!bit_set(li)) {
    return out;
  }
  for (std::uint32_t cur = levels_[li].head; cur != kNull; cur = pool_[cur].next) {
    const Node& node = pool_[cur];
    out.push_back(RestingOrder{node.id, node.qty, node.seq});
  }
  return out;
}

void DenseBookSide::apply_fills(const Price price,
                                const std::vector<std::pair<OrderId, Qty>>& fills) {
  if (level_count_ == 0 || price < min_price_ || price > max_price_) {
    return;
  }
  const std::size_t li = price_index(price);
  Level&            lv = levels_[li];

  for (const auto& [maker_id, fill_qty] : fills) {
    for (std::uint32_t cur = lv.head; cur != kNull; cur = pool_[cur].next) {
      if (pool_[cur].id == maker_id) {
        pool_[cur].qty -= fill_qty;
        lv.total_qty -= fill_qty;
        break;
      }
    }
  }

  std::uint32_t cur = lv.head;
  while (cur != kNull) {
    const std::uint32_t next = pool_[cur].next;
    if (pool_[cur].qty == 0) {
      unlink_node(price, cur);
    }
    cur = next;
  }
}

bool DenseBookSide::level_nonempty(const Price price) const {
  if (level_count_ == 0 || price < min_price_ || price > max_price_) {
    return false;
  }
  return bit_set(price_index(price));
}

}  // namespace lobcore::detail
