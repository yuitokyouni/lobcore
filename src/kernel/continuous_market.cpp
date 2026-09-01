#include <lobcore/kernel/market.hpp>

#include <lobcore/kernel/kernel.hpp>

namespace lobcore {

ContinuousMarket::ContinuousMarket(std::unique_ptr<AllocationRule> rule)
    : writer_(book_), rule_(std::move(rule)) {}

void ContinuousMarket::apply(const OrderMessage& msg, const Timestamp received_at) {
  (void)rule_;
  if (const auto* add = std::get_if<AddLimit>(&msg)) {
    const Order order{add->id, add->side, add->price, add->qty, add->decided_at};
    book_.add_limit(order, received_at);
    return;
  }
  if (const auto* cancel = std::get_if<CancelOrder>(&msg)) {
    book_.cancel(cancel->id);
  }
}

void ContinuousMarket::on_order(const OrderMessage& msg, Timestamp received_at, MarketContext& ctx) {
  (void)rule_;
  if (const auto* add = std::get_if<AddLimit>(&msg)) {
    const Order order{add->id, add->side, add->price, add->qty, add->decided_at};
    writer_.add_limit(order, received_at, [&ctx](const LogRecord& rec) { ctx.emit(rec); });
    return;
  }
  if (const auto* cancel = std::get_if<CancelOrder>(&msg)) {
    writer_.cancel(cancel->id, received_at, [&ctx](const LogRecord& rec) { ctx.emit(rec); });
  }
}

void ContinuousMarket::on_time(MarketTimerId /*timer*/, Timestamp /*time*/, MarketContext& /*ctx*/) {}

std::optional<Level> ContinuousMarket::best_bid() const { return book_.best_bid(); }

std::optional<Level> ContinuousMarket::best_ask() const { return book_.best_ask(); }

std::optional<Qty> ContinuousMarket::remaining(const OrderId id) const { return book_.remaining(id); }

std::uint64_t ContinuousMarket::state_hash() const noexcept { return book_.state_hash(); }

MarketContext::MarketContext(Kernel& kernel, const MarketId market_id)
    : kernel_(kernel), market_id_(market_id) {}

Timestamp MarketContext::now() const noexcept { return kernel_.now(); }

void MarketContext::notify(const AgentId to, const NotificationPayload& payload) {
  const Timestamp time = kernel_.now() + kernel_.latency().notify_delay(market_id_, to);
  kernel_.schedule(time, Notification{to, payload});
}

void MarketContext::schedule_timer(const MarketTimerId timer, const Timestamp time) {
  kernel_.schedule(time, MarketTimeEvent{market_id_, timer});
}

void MarketContext::emit(const LogRecord& record) { kernel_.emitted_log_.push_back(record); }

KernelView MarketContext::view() const noexcept { return KernelView(kernel_); }

}  // namespace lobcore
