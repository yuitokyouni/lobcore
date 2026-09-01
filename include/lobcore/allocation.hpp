#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <lobcore/kernel/allocation_rule.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

struct LevelMaker {
  OrderId       id;
  Qty           qty;
  std::uint64_t seq;
};

struct LevelFill {
  OrderId maker_id;
  Qty     qty;
};

enum class AllocateStatus : std::uint8_t { Ok, Overflow };

struct AllocateResult {
  AllocateStatus           status = AllocateStatus::Ok;
  std::vector<LevelFill> fills;
};

// T * qi が int64 に収まるか。積は計算しない。
[[nodiscard]] inline bool allocation_product_would_overflow(Qty take, Qty resting_qty) noexcept {
  if (take == 0) {
    return false;
  }
  return resting_qty > std::numeric_limits<Qty>::max() / take;
}

class AllocationRule {
 public:
  virtual ~AllocationRule() = default;

  // 同一価格レベル内の全 maker に対し、take 数量を配分する。
  // fills は seq 昇順。qty == 0 の maker は含めない。
  [[nodiscard]] virtual AllocateResult allocate_level(const LevelMaker* makers,
                                                    std::size_t         maker_count,
                                                    Qty                 take) const = 0;
};

class PriceTimePriority final : public AllocationRule {
 public:
  [[nodiscard]] AllocateResult allocate_level(const LevelMaker* makers,
                                              std::size_t         maker_count,
                                              Qty                 take) const override;
};

class ProRata final : public AllocationRule {
 public:
  [[nodiscard]] AllocateResult allocate_level(const LevelMaker* makers,
                                              std::size_t         maker_count,
                                              Qty                 take) const override;
};

}  // namespace lobcore
