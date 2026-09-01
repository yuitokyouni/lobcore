#pragma once

#include <cstdint>
#include <variant>

#include <lobcore/kernel/types.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

enum class ScheduledEventKind : std::uint8_t {
  OrderDelivery   = 0,
  MarketTimeEvent = 1,
  Notification    = 2,
  AgentWakeup     = 3,
};

struct OrderDelivery {
  AgentId      from;
  MarketId     to;
  OrderMessage msg;
};

struct MarketTimeEvent {
  MarketId      market;
  MarketTimerId timer;
};

struct Notification {
  AgentId              to;
  NotificationPayload payload;
};

struct AgentWakeup {
  AgentId agent;
};

using EventBody = std::variant<OrderDelivery, MarketTimeEvent, Notification, AgentWakeup>;

struct Event {
  Timestamp     time       = 0;
  std::uint8_t  kind_order = 0;  // EventBody::index() と一致
  std::uint64_t seq        = 0;  // 核が挿入時に振る
  EventBody     body;
};

[[nodiscard]] constexpr bool event_less(const Event& a, const Event& b) noexcept {
  if (a.time != b.time) {
    return a.time < b.time;
  }
  if (a.kind_order != b.kind_order) {
    return a.kind_order < b.kind_order;
  }
  return a.seq < b.seq;
}

}  // namespace lobcore
