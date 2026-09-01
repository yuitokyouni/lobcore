#pragma once

#include <memory>
#include <optional>

#include <lobcore/book.hpp>
#include <lobcore/event_log.hpp>
#include <lobcore/kernel/allocation_rule.hpp>
#include <lobcore/kernel/order_message.hpp>
#include <lobcore/kernel/types.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

class Kernel;
class KernelView;

class MarketContext {
 public:
  [[nodiscard]] Timestamp now() const noexcept;

  void notify(AgentId to, const NotificationPayload& payload);
  void schedule_timer(MarketTimerId timer, Timestamp time);
  void emit(const LogRecord& record);

  [[nodiscard]] KernelView view() const noexcept;

 private:
  friend class Kernel;

  MarketContext(Kernel& kernel, MarketId market_id);

  Kernel&   kernel_;
  MarketId  market_id_;
};

class Market {
 public:
  virtual ~Market() = default;

  virtual void on_order(const OrderMessage& msg, Timestamp received_at, MarketContext& ctx) = 0;
  virtual void on_time(MarketTimerId timer, Timestamp time, MarketContext& ctx)             = 0;

  [[nodiscard]] virtual std::optional<Level> best_bid() const      = 0;
  [[nodiscard]] virtual std::optional<Level> best_ask() const        = 0;
  [[nodiscard]] virtual std::optional<Qty>   remaining(OrderId id) const = 0;
  [[nodiscard]] virtual std::uint64_t        state_hash() const noexcept = 0;
};

class ContinuousMarket final : public Market {
 public:
  explicit ContinuousMarket(std::unique_ptr<AllocationRule> rule = std::make_unique<PriceTimePriority>());

  void on_order(const OrderMessage& msg, Timestamp received_at, MarketContext& ctx) override;
  void on_time(MarketTimerId timer, Timestamp time, MarketContext& ctx) override;

  [[nodiscard]] std::optional<Level> best_bid() const override;
  [[nodiscard]] std::optional<Level> best_ask() const override;
  [[nodiscard]] std::optional<Qty>   remaining(OrderId id) const override;
  [[nodiscard]] std::uint64_t        state_hash() const noexcept override;

  [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
  [[nodiscard]] RejectCounts     rejects() const noexcept { return book_.rejects(); }

  // MarketContext を介さずメッセージを適用する (テスト・直接駆動用)。
  void apply(const OrderMessage& msg, Timestamp received_at);

 private:
  OrderBook                       book_;
  BookEventLogWriter              writer_;
  std::unique_ptr<AllocationRule> rule_;
};

}  // namespace lobcore
