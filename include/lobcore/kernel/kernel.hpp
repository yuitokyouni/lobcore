#pragma once

#include <cstdint>
#include <vector>

#include <lobcore/kernel/config.hpp>
#include <lobcore/kernel/event.hpp>
#include <lobcore/kernel/event_heap.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

// Phase 1: イベントヒープと run ループのみ。Agent / Market は後続フェーズ。
class Kernel {
 public:
  explicit Kernel(KernelConfig config = {});

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

 private:
  void dispatch(const Event& event);

  KernelConfig          config_;
  Timestamp             now_              = 0;
  EventHeap             heap_;
  std::uint64_t         next_event_seq_   = 0;
  std::uint64_t         events_processed_ = 0;
  std::vector<Event>    processed_;
};

}  // namespace lobcore
