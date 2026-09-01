#pragma once

#include <cstddef>
#include <queue>
#include <vector>

#include <lobcore/kernel/event.hpp>

namespace lobcore {

// (time, kind_order, seq) の辞書順で最小のイベントを先に返す。
class EventHeap {
 public:
  [[nodiscard]] bool empty() const noexcept { return heap_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return heap_.size(); }

  const Event& top() const { return heap_.top(); }

  void push(Event event) { heap_.push(std::move(event)); }

  void pop() { heap_.pop(); }

  void clear() {
    while (!heap_.empty()) {
      heap_.pop();
    }
  }

 private:
  struct LaterFirst {
    bool operator()(const Event& a, const Event& b) const noexcept {
      return event_less(b, a);
    }
  };

  std::priority_queue<Event, std::vector<Event>, LaterFirst> heap_;
};

}  // namespace lobcore
