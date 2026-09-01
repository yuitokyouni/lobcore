#pragma once

#include <cstdint>
#include <vector>

#include <lobcore/book.hpp>

namespace lobcore {

enum class EventKind : std::uint8_t { Add = 0, Fill, Cancel, Reject };

enum class RejectReason : std::uint8_t {
  None = 0,
  NonPositiveQty,
  DuplicateOrderId,
  NonMonotonicTimestamp,
};

struct LogRecord {
  std::uint64_t seq = 0;
  EventKind     kind{};
  Side          side{};
  RejectReason  reason = RejectReason::None;
  Timestamp     decided_at = 0;
  Timestamp     received_at = 0;
  OrderId       order_id = 0;
  OrderId       maker_id = 0;
  Price         price = 0;
  Qty           qty = 0;
  Price         best_bid_price = 0;
  Qty           best_bid_qty = 0;
  Price         best_ask_price = 0;
  Qty           best_ask_qty = 0;
};

class LoggedBook {
 public:
  std::vector<Trade> add_limit(const Order& order, Timestamp received_at);
  bool               cancel(OrderId id, Timestamp received_at);

  const std::vector<LogRecord>& log() const noexcept { return log_; }
  const OrderBook&              book() const noexcept { return book_; }

 private:
  OrderBook              book_;
  std::vector<LogRecord> log_;
  std::uint64_t          next_acceptance_seq_ = 1;
};

OrderBook replay(const std::vector<LogRecord>& log);

}  // namespace lobcore
