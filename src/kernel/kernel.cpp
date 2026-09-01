#include <lobcore/kernel/kernel.hpp>

#include <cassert>

namespace lobcore {

Kernel::Kernel(KernelConfig config) : config_(config) {}

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

  while (!heap_.empty()) {
    const Event next = heap_.top();
    if (next.time > config_.end_time) {
      break;
    }
    if (config_.max_events > 0 && events_processed_ >= config_.max_events) {
      break;
    }

    now_ = next.time;
    dispatch(next);
    heap_.pop();
    ++events_processed_;
  }
}

void Kernel::dispatch(const Event& event) {
  processed_.push_back(event);
}

}  // namespace lobcore
