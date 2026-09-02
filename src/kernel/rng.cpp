#include <lobcore/kernel/rng.hpp>

#include <cmath>
#include <limits>

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

double Rng::uniform() {
  return static_cast<double>(next_u64() >> 11) * 0x1.0p-53;
}

double Rng::normal(double mu, double sigma) {
  if (has_spare_normal_) {
    has_spare_normal_ = false;
    return mu + sigma * spare_normal_;
  }
  double u1 = 0.0;
  double u2 = 0.0;
  do {
    u1 = uniform();
  } while (u1 <= std::numeric_limits<double>::min());
  u2                 = uniform();
  const double r     = std::sqrt(-2.0 * std::log(u1));
  const double theta = 2.0 * 3.14159265358979323846 * u2;
  spare_normal_      = r * std::sin(theta);
  has_spare_normal_  = true;
  return mu + sigma * (r * std::cos(theta));
}

double Rng::exponential(double rate) {
  double u = 0.0;
  do {
    u = uniform();
  } while (u <= std::numeric_limits<double>::min());
  return -std::log(u) / rate;
}

Rng make_rng(std::uint64_t master_seed, StreamKey key) {
  std::uint64_t seed = mix_seed(master_seed, key);
  Rng           rng;
  rng.state_[0] = splitmix64(seed);
  rng.state_[1] = splitmix64(seed);
  rng.state_[2] = splitmix64(seed);
  rng.state_[3] = splitmix64(seed);
  return rng;
}

}  // namespace lobcore
