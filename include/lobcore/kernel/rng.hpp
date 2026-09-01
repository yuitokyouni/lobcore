#pragma once

#include <cstdint>

namespace lobcore {

using ComponentId = std::uint32_t;

struct StreamKey {
  std::uint64_t agent_id;
  std::uint64_t component_id;
};

class Rng {
 public:
  [[nodiscard]] std::uint64_t next_u64();

 private:
  friend Rng make_rng(std::uint64_t master_seed, StreamKey key);

  std::uint64_t state_[4]{};
};

[[nodiscard]] Rng make_rng(std::uint64_t master_seed, StreamKey key);

}  // namespace lobcore
