#include <lobcore/event_log.hpp>

#include <cstring>

namespace lobcore {

namespace {

std::uint64_t fnv1a_byte(std::uint64_t hash, const std::uint8_t byte) {
  hash ^= static_cast<std::uint64_t>(byte);
  return hash * 1099511628211ULL;
}

std::uint64_t fnv1a_u64(std::uint64_t hash, const std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    hash = fnv1a_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
  }
  return hash;
}

std::uint64_t fnv1a_i64(std::uint64_t hash, const std::int64_t value) {
  return fnv1a_u64(hash, static_cast<std::uint64_t>(value));
}

void fill_best_levels(LogRecord& rec, const OrderBook& book) {
  const auto bid = book.best_bid();
  const auto ask = book.best_ask();
  rec.best_bid_price = bid ? bid->price : 0;
  rec.best_bid_qty   = bid ? bid->qty : 0;
  rec.best_ask_price = ask ? ask->price : 0;
  rec.best_ask_qty   = ask ? ask->qty : 0;
}

RejectReason detect_reject_reason(const RejectCounts& before, const RejectCounts& after) {
  if (after.non_positive_qty > before.non_positive_qty) {
    return RejectReason::NonPositiveQty;
  }
  if (after.duplicate_order_id > before.duplicate_order_id) {
    return RejectReason::DuplicateOrderId;
  }
  if (after.non_monotonic_timestamp > before.non_monotonic_timestamp) {
    return RejectReason::NonMonotonicTimestamp;
  }
  if (after.allocation_overflow > before.allocation_overflow) {
    return RejectReason::AllocationOverflow;
  }
  return RejectReason::None;
}

}  // namespace

std::vector<Trade> BookEventLogWriter::add_limit(const Order& order, Timestamp received_at,
                                                 const LogEmitFn& emit) {
  // Aggregate {} initializes members, but does not guarantee padding bytes.
  // Zero the entire representation before filling any LogRecord fields.
  LogRecord snapshot{};
  std::memset(&snapshot, 0, sizeof(snapshot));
  fill_best_levels(snapshot, book_);

  const RejectCounts rejects_before = book_.rejects();
  const auto         trades         = book_.add_limit(order, received_at);
  const RejectCounts rejects_after  = book_.rejects();

  const RejectReason reject_reason = detect_reject_reason(rejects_before, rejects_after);
  if (reject_reason != RejectReason::None) {
    LogRecord rec{};
    std::memset(&rec, 0, sizeof(rec));
    rec.kind             = EventKind::Reject;
    rec.side             = order.side;
    rec.reason           = reject_reason;
    rec.decided_at       = order.decided_at;
    rec.received_at      = received_at;
    rec.order_id         = order.id;
    rec.price            = order.price;
    rec.qty              = order.qty;
    rec.best_bid_price   = snapshot.best_bid_price;
    rec.best_bid_qty     = snapshot.best_bid_qty;
    rec.best_ask_price   = snapshot.best_ask_price;
    rec.best_ask_qty     = snapshot.best_ask_qty;
    emit(rec);
    return trades;
  }

  const std::uint64_t seq = next_acceptance_seq_++;

  LogRecord add_rec{};
  std::memset(&add_rec, 0, sizeof(add_rec));
  add_rec.seq              = seq;
  add_rec.kind             = EventKind::Add;
  add_rec.side             = order.side;
  add_rec.decided_at       = order.decided_at;
  add_rec.received_at      = received_at;
  add_rec.order_id         = order.id;
  add_rec.price            = order.price;
  add_rec.qty              = order.qty;
  add_rec.best_bid_price   = snapshot.best_bid_price;
  add_rec.best_bid_qty     = snapshot.best_bid_qty;
  add_rec.best_ask_price   = snapshot.best_ask_price;
  add_rec.best_ask_qty     = snapshot.best_ask_qty;
  emit(add_rec);

  for (const auto& trade : trades) {
    LogRecord fill_rec{};
    std::memset(&fill_rec, 0, sizeof(fill_rec));
    fill_rec.seq              = seq;
    fill_rec.kind             = EventKind::Fill;
    fill_rec.side             = order.side;
    fill_rec.decided_at       = order.decided_at;
    fill_rec.received_at      = received_at;
    fill_rec.order_id         = trade.taker_id;
    fill_rec.maker_id         = trade.maker_id;
    fill_rec.price            = trade.price;
    fill_rec.qty              = trade.qty;
    fill_rec.best_bid_price   = snapshot.best_bid_price;
    fill_rec.best_bid_qty     = snapshot.best_bid_qty;
    fill_rec.best_ask_price   = snapshot.best_ask_price;
    fill_rec.best_ask_qty     = snapshot.best_ask_qty;
    emit(fill_rec);
  }

  return trades;
}

bool BookEventLogWriter::cancel(OrderId id, Timestamp received_at, const LogEmitFn& emit) {
  LogRecord snapshot{};
  std::memset(&snapshot, 0, sizeof(snapshot));
  fill_best_levels(snapshot, book_);

  const auto side = book_.resting_side(id);
  const bool ok   = book_.cancel(id);
  if (!ok) {
    return false;
  }

  LogRecord rec{};
  std::memset(&rec, 0, sizeof(rec));
  rec.kind             = EventKind::Cancel;
  rec.side             = side.value_or(Side::Buy);
  rec.received_at      = received_at;
  rec.order_id         = id;
  rec.best_bid_price   = snapshot.best_bid_price;
  rec.best_bid_qty     = snapshot.best_bid_qty;
  rec.best_ask_price   = snapshot.best_ask_price;
  rec.best_ask_qty     = snapshot.best_ask_qty;
  emit(rec);
  return true;
}

std::vector<Trade> LoggedBook::add_limit(const Order& order, Timestamp received_at) {
  return writer_.add_limit(order, received_at, [this](const LogRecord& rec) { log_.push_back(rec); });
}

LoggedBook::LoggedBook(std::unique_ptr<AllocationRule> rule)
    : book_(std::move(rule)), writer_(book_) {}

bool LoggedBook::cancel(OrderId id, Timestamp received_at) {
  return writer_.cancel(id, received_at, [this](const LogRecord& rec) { log_.push_back(rec); });
}

OrderBook replay(const std::vector<LogRecord>& log) {
  return replay(log, nullptr);
}

OrderBook replay(const std::vector<LogRecord>& log, std::unique_ptr<AllocationRule> rule) {
  OrderBook book(std::move(rule));
  for (const auto& rec : log) {
    if (rec.kind == EventKind::Add || rec.kind == EventKind::Reject) {
      const Order order{rec.order_id, rec.side, rec.price, rec.qty, rec.decided_at};
      book.add_limit(order, rec.received_at);
    } else if (rec.kind == EventKind::Cancel) {
      book.cancel(rec.order_id);
    }
  }
  return book;
}

std::uint64_t log_hash(const std::vector<LogRecord>& log) noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto& rec : log) {
    hash = fnv1a_u64(hash, rec.seq);
    hash = fnv1a_byte(hash, static_cast<std::uint8_t>(rec.kind));
    hash = fnv1a_byte(hash, static_cast<std::uint8_t>(rec.side));
    hash = fnv1a_byte(hash, static_cast<std::uint8_t>(rec.reason));
    hash = fnv1a_i64(hash, rec.decided_at);
    hash = fnv1a_i64(hash, rec.received_at);
    hash = fnv1a_u64(hash, rec.order_id);
    hash = fnv1a_u64(hash, rec.maker_id);
    hash = fnv1a_i64(hash, rec.price);
    hash = fnv1a_i64(hash, rec.qty);
    hash = fnv1a_i64(hash, rec.best_bid_price);
    hash = fnv1a_i64(hash, rec.best_bid_qty);
    hash = fnv1a_i64(hash, rec.best_ask_price);
    hash = fnv1a_i64(hash, rec.best_ask_qty);
  }
  return hash;
}

}  // namespace lobcore
