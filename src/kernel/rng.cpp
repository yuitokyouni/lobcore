#include <lobcore/kernel/rng.hpp>

namespace lobcore {

namespace {

std::uint64_t rotl(std::uint64_t x, int k) {
  return (x << k) | (x >> (64 - k));
}

std::uint64_t splitmix64(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
  z               = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z               = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

std::uint64_t mix_seed(std::uint64_t master_seed, StreamKey key) {
  std::uint64_t s = master_seed;
  s ^= key.agent_id + 0x9E3779B97F4A7C15ULL + (s << 6) + (s >> 2);
  s ^= key.component_id + 0x9E3779B97F4A7C15ULL + (s << 6) + (s >> 2);
  return s;
}

}  // namespace

std::uint64_t Rng::next_u64() {
  const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
  const std::uint64_t t      = state_[1] << 17;

  state_[2] ^= state_[0];
  state_[3] ^= state_[1];
  state_[1] ^= state_[2];
  state_[0] ^= state_[3];
  state_[2] ^= t;
  state_[3] = rotl(state_[3], 45);
  return result;
}

Rng make_rng(std::uint64_t master_seed, StreamKey key) {
  std::uint64_t       seed = mix_seed(master_seed, key);
  Rng                 rng;
  rng.state_[0]            = splitmix64(seed);
  rng.state_[1]            = splitmix64(seed);
  rng.state_[2]            = splitmix64(seed);
  rng.state_[3]            = splitmix64(seed);
  return rng;
}

}  // namespace lobcore
