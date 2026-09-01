#pragma once

namespace lobcore {

// 配分規則の差し替えポイント。ContinuousMarket の既定は PriceTimePriority で、
// 実マッチングは OrderBook の価格時間優先に委譲する。
class AllocationRule {
 public:
  virtual ~AllocationRule() = default;
};

class PriceTimePriority final : public AllocationRule {};

}  // namespace lobcore
