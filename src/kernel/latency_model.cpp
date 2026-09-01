#include <lobcore/kernel/latency_model.hpp>

#include <stdexcept>

namespace lobcore {

void LatencyModel::require_positive_delay(const Timestamp delay) {
  if (delay < 1) {
    throw std::invalid_argument("latency delay must be >= 1");
  }
}

Timestamp LatencyModel::order_delay(const AgentId from, const MarketId to) const {
  (void)from;
  (void)to;
  const auto it = agent_overrides_.find(from);
  if (it != agent_overrides_.end()) {
    return it->second;
  }
  return default_delay_;
}

Timestamp LatencyModel::notify_delay(const MarketId from, const AgentId to) const {
  (void)from;
  (void)to;
  return default_delay_;
}

void LatencyModel::set_agent_delay(const AgentId agent, const Timestamp delay) {
  require_positive_delay(delay);
  agent_overrides_[agent] = delay;
}

}  // namespace lobcore
