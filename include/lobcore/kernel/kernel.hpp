#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include <lobcore/kernel/agent.hpp>
#include <lobcore/kernel/config.hpp>
#include <lobcore/kernel/event.hpp>
#include <lobcore/kernel/event_heap.hpp>
#include <lobcore/kernel/rng.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

class Kernel {
 public:
  explicit Kernel(KernelConfig config = {});

  AgentId add_agent(std::unique_ptr<Agent> agent);

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

 private:
  friend class AgentContext;
  friend class KernelView;

  struct AgentSlot {
    std::unique_ptr<Agent>                 agent;
    std::unordered_map<ComponentId, Rng> rng_by_component;
  };

  [[nodiscard]] Rng& agent_rng(AgentId id, ComponentId component);
  [[nodiscard]] bool schedule_agent_wakeup(AgentId agent, Timestamp time);

  void dispatch(const Event& event);
  void dispatch_agent_wakeup(AgentId agent);
  void dispatch_notification(AgentId to, const NotificationPayload& payload);

  KernelConfig       config_;
  Timestamp          now_              = 0;
  EventHeap          heap_;
  std::uint64_t      next_event_seq_   = 0;
  std::uint64_t      events_processed_ = 0;
  bool               finished_         = false;
  std::vector<AgentSlot> agents_;
  std::vector<Event> processed_;
};

}  // namespace lobcore
