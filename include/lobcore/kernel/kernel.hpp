#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <lobcore/event_log.hpp>
#include <lobcore/kernel/agent.hpp>
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

 private:
  friend class AgentContext;
  friend class KernelView;
  friend class MarketContext;

  struct AgentSlot {
    std::unique_ptr<Agent>                 agent;
    std::unordered_map<ComponentId, Rng> rng_by_component;
  };

  [[nodiscard]] Rng& agent_rng(AgentId id, ComponentId component);
  [[nodiscard]] bool schedule_agent_wakeup(AgentId agent, Timestamp time);
  void               submit_order(AgentId from, MarketId to, OrderMessage msg);

  void dispatch(const Event& event);
  void dispatch_order_delivery(const OrderDelivery& delivery);
  void dispatch_market_time_event(const MarketTimeEvent& event);
  void dispatch_agent_wakeup(AgentId agent);
  void dispatch_notification(AgentId to, const NotificationPayload& payload);

  [[nodiscard]] const Market& market(MarketId id) const;

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
};

}  // namespace lobcore
