#include <lobcore/kernel/kernel.hpp>

#include <cassert>
#include <stdexcept>
#include <utility>

namespace lobcore {

Kernel::Kernel(KernelConfig config) : config_(config) {}

AgentId Kernel::add_agent(std::unique_ptr<Agent> agent) {
  if (finished_) {
    throw std::logic_error("cannot add agents after Kernel::run");
  }
  const AgentId id = static_cast<AgentId>(agents_.size());
  agents_.push_back(AgentSlot{.agent = std::move(agent), .rng_by_component = {}});
  return id;
}

void Kernel::schedule(Timestamp time, EventBody body) {
  Event event;
  event.time       = time;
  event.kind_order = static_cast<std::uint8_t>(body.index());
  event.seq        = next_event_seq_++;
  event.body       = std::move(body);
  assert(event.kind_order == static_cast<std::uint8_t>(event.body.index()));
  heap_.push(std::move(event));
}

void Kernel::run() {
  processed_.clear();
  events_processed_ = 0;
  finished_         = true;

  while (!heap_.empty()) {
    const Event next = heap_.top();
    if (next.time > config_.end_time) {
      break;
    }
    if (config_.max_events.has_value() && events_processed_ >= *config_.max_events) {
      break;
    }

    now_ = next.time;
    dispatch(next);
    heap_.pop();
    ++events_processed_;
  }
}

Rng& Kernel::agent_rng(AgentId id, ComponentId component) {
  assert(id < agents_.size());
  auto& slot = agents_[id];
  const auto it = slot.rng_by_component.find(component);
  if (it != slot.rng_by_component.end()) {
    return it->second;
  }
  const StreamKey key{.agent_id = id, .component_id = component};
  auto [inserted, ok] = slot.rng_by_component.emplace(component, make_rng(config_.master_seed, key));
  assert(ok);
  return inserted->second;
}

bool Kernel::schedule_agent_wakeup(AgentId agent, Timestamp time) {
  if (time <= now_) {
    return false;
  }
  if (agent >= agents_.size()) {
    return false;
  }
  schedule(time, AgentWakeup{agent});
  return true;
}

void Kernel::dispatch(const Event& event) {
  processed_.push_back(event);

  if (const auto* wakeup = std::get_if<AgentWakeup>(&event.body)) {
    dispatch_agent_wakeup(wakeup->agent);
    return;
  }
  if (const auto* notification = std::get_if<Notification>(&event.body)) {
    dispatch_notification(notification->to, notification->payload);
  }
}

void Kernel::dispatch_agent_wakeup(AgentId agent) {
  if (agent >= agents_.size()) {
    return;
  }
  KernelView   view(*this);
  AgentContext ctx(*this, agent);
  agents_[agent].agent->on_wakeup(view, ctx);
}

void Kernel::dispatch_notification(AgentId to, const NotificationPayload& payload) {
  if (to >= agents_.size()) {
    return;
  }
  KernelView   view(*this);
  AgentContext ctx(*this, to);
  agents_[to].agent->on_notification(payload, view, ctx);
}

Timestamp KernelView::now() const noexcept { return kernel_.now(); }

Timestamp AgentContext::now() const noexcept { return kernel_.now(); }

Rng& AgentContext::rng(ComponentId component) {
  return kernel_.agent_rng(id_, component);
}

bool AgentContext::schedule_wakeup(Timestamp t) { return kernel_.schedule_agent_wakeup(id_, t); }

}  // namespace lobcore
