#pragma once

namespace lobcore {

// 配分規則の差し替えポイント。
class AllocationRule {
 public:
  virtual ~AllocationRule() = default;
};

class PriceTimePriority final : public AllocationRule {};

class ProRata final : public AllocationRule {};

}  // namespace lobcore
