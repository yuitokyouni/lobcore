#include <catch2/catch_test_macros.hpp>

#include <lobcore/kernel/event.hpp>
#include <lobcore/kernel/event_heap.hpp>
#include <lobcore/kernel/kernel.hpp>

using namespace lobcore;

namespace {

Event make_wakeup(AgentId agent, Timestamp time, std::uint64_t seq, std::uint8_t kind) {
  Event e;
  e.time       = time;
  e.kind_order = kind;
  e.seq        = seq;
  e.body       = AgentWakeup{agent};
  return e;
}

std::vector<Event> drain(EventHeap& heap) {
  std::vector<Event> out;
  while (!heap.empty()) {
    out.push_back(heap.top());
    heap.pop();
  }
  return out;
}

}  // namespace

TEST_CASE("EventHeap orders by time then kind_order then seq") {
  EventHeap heap;
  heap.push(make_wakeup(1, 10, 0, static_cast<std::uint8_t>(ScheduledEventKind::AgentWakeup)));
  heap.push(make_wakeup(2, 5, 1, static_cast<std::uint8_t>(ScheduledEventKind::AgentWakeup)));
  heap.push(make_wakeup(3, 10, 2, static_cast<std::uint8_t>(ScheduledEventKind::OrderDelivery)));

  const auto ordered = drain(heap);
  REQUIRE(ordered.size() == 3);
  CHECK(ordered[0].time == 5);
  CHECK(ordered[1].time == 10);
  CHECK(ordered[1].kind_order == static_cast<std::uint8_t>(ScheduledEventKind::OrderDelivery));
  CHECK(ordered[2].kind_order == static_cast<std::uint8_t>(ScheduledEventKind::AgentWakeup));
}

TEST_CASE("EventHeap breaks ties on seq at same time and kind") {
  EventHeap heap;
  heap.push(make_wakeup(1, 7, 10, static_cast<std::uint8_t>(ScheduledEventKind::Notification)));
  heap.push(make_wakeup(2, 7, 11, static_cast<std::uint8_t>(ScheduledEventKind::Notification)));
  heap.push(make_wakeup(3, 7, 12, static_cast<std::uint8_t>(ScheduledEventKind::Notification)));

  const auto ordered = drain(heap);
  REQUIRE(ordered.size() == 3);
  CHECK(ordered[0].seq == 10);
  CHECK(ordered[1].seq == 11);
  CHECK(ordered[2].seq == 12);
}

TEST_CASE("Kernel assigns monotonic seq in schedule order") {
  Kernel kernel;
  kernel.schedule(1, AgentWakeup{1});
  kernel.schedule(1, AgentWakeup{2});
  kernel.schedule(1, AgentWakeup{3});
  kernel.run();

  const auto& processed = kernel.processed_events();
  REQUIRE(processed.size() == 3);
  CHECK(processed[0].seq == 0);
  CHECK(processed[1].seq == 1);
  CHECK(processed[2].seq == 2);
}

TEST_CASE("Kernel run advances now and processes wakeup last at same time") {
  Kernel kernel;
  kernel.schedule(5, AgentWakeup{1});
  kernel.schedule(5, OrderDelivery{0, 0, {}});
  kernel.schedule(5, MarketTimeEvent{0, 0});
  kernel.schedule(5, Notification{0, {}});
  kernel.run();

  REQUIRE(kernel.now() == 5);
  REQUIRE(kernel.processed_events().size() == 4);
  CHECK(kernel.processed_events()[0].kind_order ==
        static_cast<std::uint8_t>(ScheduledEventKind::OrderDelivery));
  CHECK(kernel.processed_events()[1].kind_order ==
        static_cast<std::uint8_t>(ScheduledEventKind::MarketTimeEvent));
  CHECK(kernel.processed_events()[2].kind_order ==
        static_cast<std::uint8_t>(ScheduledEventKind::Notification));
  CHECK(kernel.processed_events()[3].kind_order ==
        static_cast<std::uint8_t>(ScheduledEventKind::AgentWakeup));
}

TEST_CASE("Kernel run stops at end_time without consuming later events") {
  KernelConfig config;
  config.end_time = 10;
  Kernel kernel(config);

  kernel.schedule(10, AgentWakeup{1});
  kernel.schedule(11, AgentWakeup{2});
  kernel.run();

  CHECK(kernel.events_processed() == 1);
  CHECK(kernel.now() == 10);
  CHECK_FALSE(kernel.empty());
  const Event* deferred = kernel.peek_next();
  REQUIRE(deferred != nullptr);
  CHECK(deferred->time == 11);
  REQUIRE(std::holds_alternative<AgentWakeup>(deferred->body));
  CHECK(std::get<AgentWakeup>(deferred->body).agent == 2);
}

TEST_CASE("Kernel run stops at max_events and leaves remainder in heap") {
  KernelConfig config;
  config.max_events = 2;
  Kernel kernel(config);

  kernel.schedule(1, AgentWakeup{1});
  kernel.schedule(2, AgentWakeup{2});
  kernel.schedule(3, AgentWakeup{3});
  kernel.run();

  CHECK(kernel.events_processed() == 2);
  CHECK(kernel.now() == 2);
  CHECK_FALSE(kernel.empty());
  const Event* deferred = kernel.peek_next();
  REQUIRE(deferred != nullptr);
  CHECK(deferred->time == 3);
}

TEST_CASE("Kernel max_events zero processes nothing") {
  KernelConfig config;
  config.max_events = 0;
  Kernel kernel(config);

  kernel.schedule(1, AgentWakeup{1});
  kernel.run();

  CHECK(kernel.events_processed() == 0);
  CHECK(kernel.now() == 0);
  CHECK_FALSE(kernel.empty());
}
