#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <lobcore/event_log.hpp>
#include <lobcore/kernel/agent.hpp>
#include <lobcore/kernel/batch.hpp>
#include <lobcore/kernel/config.hpp>
#include <lobcore/kernel/event.hpp>
#include <lobcore/kernel/event_heap.hpp>
#include <lobcore/kernel/latency_model.hpp>
#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/rng.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

class Kernel {
 public:
  explicit Kernel(KernelConfig config = {});

  AgentId  add_agent(std::unique_ptr<Agent> agent);
  MarketId add_market(std::unique_ptr<Market> market);

  // Python 一括エージェント。n 個の AgentId を確保し、同一時刻の起床をまとめて step に渡す。
  std::vector<AgentId> add_batch_agents(BatchStepFn step, std::size_t n);

  void schedule(Timestamp time, EventBody body);

  void run();

  [[nodiscard]] Timestamp now() const noexcept { return now_; }
  [[nodiscard]] std::uint64_t events_processed() const noexcept { return events_processed_; }
  [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }
  [[nodiscard]] const Event* peek_next() const noexcept {
    return heap_.empty() ? nullptr : &heap_.top();
  }
  [[nodiscard]] const std::vector<Event>& processed_events() const noexcept {
    return processed_;
  }
  [[nodiscard]] std::size_t agent_count() const noexcept { return agents_.size(); }
  [[nodiscard]] std::size_t market_count() const noexcept { return markets_.size(); }
  [[nodiscard]] const LatencyModel& latency() const noexcept { return latency_; }
  [[nodiscard]] LatencyModel&       latency() noexcept { return latency_; }
  [[nodiscard]] const std::vector<LogRecord>& emitted_log() const noexcept { return emitted_log_; }
  [[nodiscard]] const BatchRejectCounts& batch_rejects() const noexcept { return batch_rejects_; }
  [[nodiscard]] std::uint64_t master_seed() const noexcept { return config_.master_seed; }
  [[nodiscard]] Timestamp     end_time() const noexcept { return config_.end_time; }
  [[nodiscard]] const Market& market(MarketId id) const;
  [[nodiscard]] std::uint64_t market_state_hash(MarketId id) const;

 private:
  friend class AgentContext;
  friend class KernelView;
  friend class MarketContext;

  struct AgentSlot {
    std::unique_ptr<Agent>                 agent;
    std::unordered_map<ComponentId, Rng> rng_by_component;
    bool                                 is_batch_member = false;
  };

  [[nodiscard]] Rng& agent_rng(AgentId id, ComponentId component);
  [[nodiscard]] bool schedule_agent_wakeup(AgentId agent, Timestamp time);
  void               submit_order(AgentId from, MarketId to, OrderMessage msg);

  void dispatch(const Event& event);
  void dispatch_order_delivery(const OrderDelivery& delivery);
  void dispatch_market_time_event(const MarketTimeEvent& event);
  void dispatch_agent_wakeup(AgentId agent);
  void dispatch_notification(AgentId to, const NotificationPayload& payload);
  void dispatch_agent_wakeups_at_time(std::vector<AgentId> wakeups);

  void apply_batch_action(const std::vector<AgentId>& agent_ids, const BatchAction& action);
  [[nodiscard]] BatchObservation make_batch_observation(const std::vector<AgentId>& agent_ids) const;

  KernelConfig       config_;
  Timestamp          now_              = 0;
  EventHeap          heap_;
  std::uint64_t      next_event_seq_   = 0;
  std::uint64_t      events_processed_ = 0;
  bool               finished_         = false;
  LatencyModel       latency_;
  std::vector<AgentSlot> agents_;
  std::vector<std::unique_ptr<Market>> markets_;
  std::vector<Event> processed_;
  std::vector<LogRecord> emitted_log_;

  BatchStepFn                              batch_step_;
  std::unordered_set<AgentId>              batch_members_;
  BatchRejectCounts                        batch_rejects_{};
};

}  // namespace lobcore
