#include <lobcore/allocation.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace lobcore {

namespace {

struct RemainderRank {
  std::size_t   index;
  Qty           remainder;
  std::uint64_t seq;
};

}  // namespace

AllocateResult PriceTimePriority::allocate_level(const LevelMaker* makers,
                                               const std::size_t maker_count,
                                               const Qty         take) const {
  AllocateResult result;
  result.status = AllocateStatus::Ok;
  Qty remaining = take;
  for (std::size_t i = 0; i < maker_count && remaining > 0; ++i) {
    const Qty fill = std::min(remaining, makers[i].qty);
    if (fill > 0) {
      result.fills.push_back(LevelFill{makers[i].id, fill});
      remaining -= fill;
    }
  }
  return result;
}

AllocateResult ProRata::allocate_level(const LevelMaker* makers,
                                       const std::size_t maker_count,
                                       const Qty         take) const {
  AllocateResult result;
  result.status = AllocateStatus::Ok;
  if (take == 0 || maker_count == 0) {
    return result;
  }

  Qty level_total = 0;
  for (std::size_t i = 0; i < maker_count; ++i) {
    if (allocation_product_would_overflow(take, makers[i].qty)) {
      result.status = AllocateStatus::Overflow;
      result.fills.clear();
      return result;
    }
    level_total += makers[i].qty;
  }

  if (level_total == 0) {
    return result;
  }

  std::vector<Qty> fills(maker_count, 0);
  Qty              allocated = 0;
  std::vector<RemainderRank> ranks;
  ranks.reserve(maker_count);

  for (std::size_t i = 0; i < maker_count; ++i) {
    const Qty product = take * makers[i].qty;
    const Qty base    = product / level_total;
    const Qty rem     = product % level_total;
    fills[i]          = base;
    allocated += base;
    ranks.push_back(RemainderRank{i, rem, makers[i].seq});
  }

  Qty dust = take - allocated;
  std::sort(ranks.begin(), ranks.end(), [](const RemainderRank& a, const RemainderRank& b) {
    if (a.remainder != b.remainder) {
      return a.remainder > b.remainder;
    }
    return a.seq < b.seq;
  });

  for (std::size_t i = 0; i < ranks.size() && dust > 0; ++i) {
    fills[ranks[i].index] += 1;
    --dust;
  }

  result.fills.reserve(maker_count);
  for (std::size_t i = 0; i < maker_count; ++i) {
    if (fills[i] > 0) {
      result.fills.push_back(LevelFill{makers[i].id, fills[i]});
    }
  }
  return result;
}

}  // namespace lobcore
