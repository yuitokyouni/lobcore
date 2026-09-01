#pragma once

#include <cstdint>
#include <limits>

#include <lobcore/types.hpp>

namespace lobcore {

struct KernelConfig {
  Timestamp     end_time   = std::numeric_limits<Timestamp>::max();
  std::uint64_t max_events = 0;  // 0 = 上限なし
};

}  // namespace lobcore
