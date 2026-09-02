#include <lobcore/kernel/kernel.hpp>

#include <lobcore/kernel/batch_member_agent.hpp>

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
  agents_.push_back(AgentSlot{.agent = std::move(agent), .rng_by_component = {}, .is_batch_member = false});
  return id;
}

std::vector<AgentId> Kernel::add_batch_agents(BatchStepFn step, std::size_t n) {
  if (finished_) {
    throw std::logic_error("cannot add agents after Kernel::run");
  }
  if (!step) {
    throw std::invalid_argument("batch step function is empty");
  }
  if (batch_step_ && !batch_members_.empty()) {
    throw std::logic_error("only one python batch is supported in Stage 5");
  }
  batch_step_ = std::move(step);
  std::vector<AgentId> ids;
  ids.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const AgentId id = static_cast<AgentId>(agents_.size());
    agents_.push_back(AgentSlot{.agent           = std::make_unique<BatchMemberAgent>(),
                                .rng_by_component = {},
                                .is_batch_member = true});
    batch_members_.insert(id);
    ids.push_back(id);
  }
  return ids;
}

MarketId Kernel::add_market(std::unique_ptr<Market> market) {
  if (finished_) {
    throw std::logic_error("cannot add markets after Kernel::run");
  }
  const MarketId id = static_cast<MarketId>(markets_.size());
  markets_.push_back(std::move(market));
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
  emitted_log_.clear();
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

    if (std::holds_alternative<AgentWakeup>(next.body)) {
      std::vector<AgentId> wakeups;
      while (!heap_.empty()) {
        const Event& top = heap_.top();
        if (top.time != now_ || !std::holds_alternative<AgentWakeup>(top.body)) {
          break;
        }
        if (config_.max_events.has_value() &&
            events_processed_ + wakeups.size() >= *config_.max_events) {
          break;
        }
        const auto* wakeup = std::get_if<AgentWakeup>(&top.body);
        wakeups.push_back(wakeup->agent);
        processed_.push_back(top);
        heap_.pop();
      }
      events_processed_ += wakeups.size();
      dispatch_agent_wakeups_at_time(std::move(wakeups));
      continue;
    }

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

void Kernel::submit_order(AgentId from, MarketId to, OrderMessage msg) {
  if (to >= markets_.size()) {
    return;
  }

  if (auto* add = std::get_if<AddLimit>(&msg)) {
    add->decided_at = now_;
  }

  const Timestamp delivery_time = now_ + latency_.order_delay(from, to);
  schedule(delivery_time, OrderDelivery{from, to, std::move(msg)});
}

const Market& Kernel::market(const MarketId id) const {
  if (id >= markets_.size()) {
    throw std::out_of_range("market id out of range");
  }
  return *markets_[id];
}

std::uint64_t Kernel::market_state_hash(MarketId id) const {
  return market(id).state_hash();
}

void Kernel::dispatch(const Event& event) {
  processed_.push_back(event);

  if (const auto* delivery = std::get_if<OrderDelivery>(&event.body)) {
    dispatch_order_delivery(*delivery);
    return;
  }
  if (const auto* market_event = std::get_if<MarketTimeEvent>(&event.body)) {
    dispatch_market_time_event(*market_event);
    return;
  }
  if (const auto* wakeup = std::get_if<AgentWakeup>(&event.body)) {
    dispatch_agent_wakeup(wakeup->agent);
    return;
  }
  if (const auto* notification = std::get_if<Notification>(&event.body)) {
    dispatch_notification(notification->to, notification->payload);
  }
}

void Kernel::dispatch_order_delivery(const OrderDelivery& delivery) {
  if (delivery.to >= markets_.size()) {
    return;
  }
  if (const auto* add = std::get_if<AddLimit>(&delivery.msg)) {
    assert(now_ > add->decided_at);
    (void)add;
  }

  MarketContext ctx(*this, delivery.to);
  markets_[delivery.to]->on_order(delivery.msg, now_, ctx);
}

void Kernel::dispatch_market_time_event(const MarketTimeEvent& event) {
  if (event.market >= markets_.size()) {
    return;
  }
  MarketContext ctx(*this, event.market);
  markets_[event.market]->on_time(event.timer, now_, ctx);
}

void Kernel::dispatch_agent_wakeup(AgentId agent) {
  if (agent >= agents_.size()) {
    return;
  }
  KernelView   view(*this);
  AgentContext ctx(*this, agent);
  agents_[agent].agent->on_wakeup(view, ctx);
}

void Kernel::dispatch_agent_wakeups_at_time(std::vector<AgentId> wakeups) {
  std::vector<AgentId> batch_ids;
  batch_ids.reserve(wakeups.size());
  for (const AgentId id : wakeups) {
    if (id < agents_.size() && agents_[id].is_batch_member) {
      batch_ids.push_back(id);
    }
  }

  bool batch_flushed = false;
  for (const AgentId id : wakeups) {
    if (id >= agents_.size()) {
      continue;
    }
    if (agents_[id].is_batch_member) {
      if (!batch_flushed && batch_step_) {
        const BatchObservation obs = make_batch_observation(batch_ids);
        const BatchAction      act = batch_step_(obs);
        apply_batch_action(batch_ids, act);
        batch_flushed = true;
      } else if (!batch_flushed) {
        dispatch_agent_wakeup(id);
      }
      continue;
    }
    dispatch_agent_wakeup(id);
  }
}

BatchObservation Kernel::make_batch_observation(const std::vector<AgentId>& agent_ids) const {
  BatchObservation obs;
  obs.now       = now_;
  obs.agent_ids = agent_ids;
  obs.markets.reserve(markets_.size());
  for (const auto& m : markets_) {
    obs.markets.push_back(MarketSnapshot{.best_bid = m->best_bid(), .best_ask = m->best_ask()});
  }
  return obs;
}

void Kernel::apply_batch_action(const std::vector<AgentId>& agent_ids, const BatchAction& action) {
  if (action.next_wakeups.size() != agent_ids.size()) {
    throw std::invalid_argument("next_wakeups size must equal agent_ids size");
  }

  std::unordered_set<AgentId> awake(agent_ids.begin(), agent_ids.end());

  for (std::size_t i = 0; i < agent_ids.size(); ++i) {
    const Timestamp t = action.next_wakeups[i];
    if (t == 0) {
      continue;
    }
    if (t <= now_) {
      ++batch_rejects_.invalid_wakeup;
      continue;
    }
    (void)schedule_agent_wakeup(agent_ids[i], t);
  }

  for (const OrderSubmission& sub : action.orders) {
    if (awake.find(sub.agent_id) == awake.end()) {
      ++batch_rejects_.order_from_sleeping_agent;
      continue;
    }
    submit_order(sub.agent_id, sub.market_id, sub.msg);
  }
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

const Market& KernelView::market(const MarketId id) const { return kernel_.market(id); }

std::size_t KernelView::market_count() const noexcept { return kernel_.market_count(); }

Timestamp AgentContext::now() const noexcept { return kernel_.now(); }

Rng& AgentContext::rng(ComponentId component) {
  return kernel_.agent_rng(id_, component);
}

void AgentContext::submit(MarketId to, const OrderMessage& msg) {
  OrderMessage copy = msg;
  kernel_.submit_order(id_, to, std::move(copy));
}

bool AgentContext::schedule_wakeup(Timestamp t) { return kernel_.schedule_agent_wakeup(id_, t); }

}  // namespace lobcore
