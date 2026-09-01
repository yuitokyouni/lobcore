#pragma once

#include <stdexcept>
#include <unordered_map>

#include <lobcore/kernel/types.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

class LatencyModel {
 public:
  [[nodiscard]] Timestamp order_delay(AgentId from, MarketId to) const;
  [[nodiscard]] Timestamp notify_delay(MarketId from, AgentId to) const;

  void set_agent_delay(AgentId agent, Timestamp delay);

 private:
  static void require_positive_delay(Timestamp delay);

  Timestamp default_delay_ = 1;
  std::unordered_map<AgentId, Timestamp> agent_overrides_;
};

}  // namespace lobcore
