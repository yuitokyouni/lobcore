#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <lobcore/book.hpp>

namespace lobcore {

enum class EventKind : std::uint8_t { Add = 0, Fill, Cancel, Reject, MarketTime };

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

  friend constexpr bool operator==(const LogRecord& a, const LogRecord& b) noexcept {
    return a.seq == b.seq && a.kind == b.kind && a.side == b.side && a.reason == b.reason &&
           a.decided_at == b.decided_at && a.received_at == b.received_at && a.order_id == b.order_id &&
           a.maker_id == b.maker_id && a.price == b.price && a.qty == b.qty &&
           a.best_bid_price == b.best_bid_price && a.best_bid_qty == b.best_bid_qty &&
           a.best_ask_price == b.best_ask_price && a.best_ask_qty == b.best_ask_qty;
  }
};

using LogEmitFn = std::function<void(const LogRecord&)>;

// OrderBook への操作と LogRecord 生成を共通化する。LoggedBook と ContinuousMarket が使う。
class BookEventLogWriter {
 public:
  explicit BookEventLogWriter(OrderBook& book) : book_(book) {}

  std::vector<Trade> add_limit(const Order& order, Timestamp received_at, const LogEmitFn& emit);
  bool               cancel(OrderId id, Timestamp received_at, const LogEmitFn& emit);

 private:
  OrderBook&    book_;
  std::uint64_t next_acceptance_seq_ = 1;
};

class LoggedBook {
 public:
  LoggedBook() : writer_(book_) {}

  std::vector<Trade> add_limit(const Order& order, Timestamp received_at);
  bool               cancel(OrderId id, Timestamp received_at);

  const std::vector<LogRecord>& log() const noexcept { return log_; }
  const OrderBook&              book() const noexcept { return book_; }

 private:
  OrderBook              book_;
  BookEventLogWriter     writer_;
  std::vector<LogRecord> log_;
};

OrderBook replay(const std::vector<LogRecord>& log);

[[nodiscard]] std::uint64_t log_hash(const std::vector<LogRecord>& log) noexcept;

}  // namespace lobcore
