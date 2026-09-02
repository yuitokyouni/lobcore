#pragma once

#include <cstddef>
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
