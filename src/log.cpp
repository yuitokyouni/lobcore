#include <lobcore/event_log.hpp>

namespace lobcore {

namespace {

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
  return RejectReason::None;
}

}  // namespace

std::vector<Trade> LoggedBook::add_limit(const Order& order, Timestamp received_at) {
  LogRecord snapshot{};
  fill_best_levels(snapshot, book_);

  const RejectCounts rejects_before = book_.rejects();
  const auto         trades         = book_.add_limit(order, received_at);
  const RejectCounts rejects_after  = book_.rejects();

  const RejectReason reject_reason = detect_reject_reason(rejects_before, rejects_after);
  if (reject_reason != RejectReason::None) {
    LogRecord rec{};
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
    log_.push_back(rec);
    return trades;
  }

  const std::uint64_t seq = next_acceptance_seq_++;

  LogRecord add_rec{};
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
  log_.push_back(add_rec);

  for (const auto& trade : trades) {
    LogRecord fill_rec{};
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
    log_.push_back(fill_rec);
  }

  return trades;
}

bool LoggedBook::cancel(OrderId id, Timestamp received_at) {
  LogRecord snapshot{};
  fill_best_levels(snapshot, book_);

  const auto side = book_.resting_side(id);
  const bool ok   = book_.cancel(id);
  if (!ok) {
    return false;
  }

  LogRecord rec{};
  rec.kind             = EventKind::Cancel;
  rec.side             = side.value_or(Side::Buy);
  rec.received_at      = received_at;
  rec.order_id         = id;
  rec.best_bid_price   = snapshot.best_bid_price;
  rec.best_bid_qty     = snapshot.best_bid_qty;
  rec.best_ask_price   = snapshot.best_ask_price;
  rec.best_ask_qty     = snapshot.best_ask_qty;
  log_.push_back(rec);
  return true;
}

OrderBook replay(const std::vector<LogRecord>& log) {
  OrderBook book;
  for (const auto& rec : log) {
    if (rec.kind == EventKind::Add) {
      const Order order{rec.order_id, rec.side, rec.price, rec.qty, rec.decided_at};
      book.add_limit(order, rec.received_at);
    } else if (rec.kind == EventKind::Cancel) {
      book.cancel(rec.order_id);
    }
  }
  return book;
}

}  // namespace lobcore
