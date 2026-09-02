#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace lobcore {

using ComponentId = std::uint32_t;

// エージェントに属さない exogenous 系列（fundamental 等）用。要件 4。
inline constexpr std::uint64_t kSentinelAgentId = std::numeric_limits<std::uint64_t>::max();

struct StreamKey {
  std::uint64_t agent_id;
  std::uint64_t component_id;
};

class Rng {
 public:
  [[nodiscard]] std::uint64_t next_u64();

  // [0, 1)。環境非依存のため自前実装。
  [[nodiscard]] double uniform();
  [[nodiscard]] double normal(double mu, double sigma);
  [[nodiscard]] double exponential(double rate);

 private:
  friend Rng make_rng(std::uint64_t master_seed, StreamKey key);

  std::uint64_t state_[4]{};
  bool          has_spare_normal_ = false;
  double        spare_normal_     = 0.0;
};

[[nodiscard]] Rng make_rng(std::uint64_t master_seed, StreamKey key);

}  // namespace lobcore
