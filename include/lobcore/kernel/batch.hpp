#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include <lobcore/book.hpp>
#include <lobcore/kernel/order_message.hpp>
#include <lobcore/kernel/types.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

struct MarketSnapshot {
  std::optional<Level> best_bid;
  std::optional<Level> best_ask;
};

struct BatchObservation {
  Timestamp                   now = 0;
  std::vector<AgentId>        agent_ids;  // seq 順
  std::vector<MarketSnapshot> markets;
};

struct OrderSubmission {
  AgentId      agent_id = 0;
  MarketId     market_id = 0;
  OrderMessage msg;
};

struct BatchAction {
  std::vector<OrderSubmission> orders;
  std::vector<Timestamp>       next_wakeups;  // agent_ids と同じ長さ。0 = 起きない
};

struct BatchRejectCounts {
  std::uint64_t invalid_wakeup            = 0;
  std::uint64_t order_from_sleeping_agent = 0;
};

using BatchStepFn = std::function<BatchAction(const BatchObservation&)>;

}  // namespace lobcore
