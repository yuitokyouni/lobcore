#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include <lobcore/types.hpp>

namespace lobcore {

struct KernelConfig {
  Timestamp end_time = std::numeric_limits<Timestamp>::max();
  // nullopt = 上限なし。0 を指定すれば 0 件で停止する。
  std::optional<std::uint64_t> max_events;
  std::uint64_t master_seed = 0;
};

}  // namespace lobcore
