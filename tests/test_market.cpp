#include <catch2/catch_test_macros.hpp>

#include <lobcore/book.hpp>
#include <lobcore/kernel/kernel.hpp>
#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/order_message.hpp>
#include <lobcore/allocation.hpp>

#include <memory>

using namespace lobcore;

namespace {

AddLimit buy_msg(OrderId id, Price price, Qty qty) {
  return AddLimit{id, Side::Buy, price, qty, 0};
}

AddLimit sell_msg(OrderId id, Price price, Qty qty) {
  return AddLimit{id, Side::Sell, price, qty, 0};
}

class SubmitOnceAgent final : public Agent {
 public:
  explicit SubmitOnceAgent(MarketId market, OrderMessage msg)
      : market_(market), msg_(std::move(msg)) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    ctx.submit(market_, msg_);
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId     market_;
  OrderMessage msg_;
  bool         done_ = false;
};

class ObserveOnlyAgent final : public Agent {
 public:
  explicit ObserveOnlyAgent(MarketId market, std::uint64_t* observed_bid_qty)
      : market_(market), observed_bid_qty_(observed_bid_qty) {}

  void on_wakeup(const KernelView& view, AgentContext& /*ctx*/) override {
    if (const auto bid = view.market(market_).best_bid()) {
      *observed_bid_qty_ = static_cast<std::uint64_t>(bid->qty);
    } else {
      *observed_bid_qty_ = 0;
    }
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId       market_;
  std::uint64_t* observed_bid_qty_;
};

class BuyerAgent final : public Agent {
 public:
  BuyerAgent(MarketId market, bool* submitted) : market_(market), submitted_(submitted) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (*submitted_) {
      return;
    }
    *submitted_ = true;
    ctx.submit(market_, buy_msg(1, 100, 7));
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  bool*    submitted_;
};

}  // namespace

TEST_CASE("ContinuousMarket matches OrderBook for a sample sequence") {
  OrderBook book;
  ContinuousMarket market;

  const auto step = [&](Timestamp t, const OrderMessage& msg) {
    if (const auto* add = std::get_if<AddLimit>(&msg)) {
      const Order order{add->id, add->side, add->price, add->qty, t};
      book.add_limit(order, t);
      market.apply(msg, t);
      return;
    }
    if (const auto* cancel = std::get_if<CancelOrder>(&msg)) {
      book.cancel(cancel->id);
      market.apply(msg, t);
    }
  };

  step(1, buy_msg(1, 100, 10));
  step(2, sell_msg(2, 100, 4));
  step(3, buy_msg(3, 99, 5));
  step(4, CancelOrder{1});
  step(5, sell_msg(4, 101, 3));

  CHECK(market.best_bid() == book.best_bid());
  CHECK(market.best_ask() == book.best_ask());
  CHECK(market.state_hash() == book.state_hash());
  CHECK(market.rejects().duplicate_order_id == book.rejects().duplicate_order_id);
  CHECK(market.rejects().non_positive_qty == book.rejects().non_positive_qty);
  CHECK(market.rejects().non_monotonic_timestamp == book.rejects().non_monotonic_timestamp);
  CHECK(market.rejects().allocation_overflow == book.rejects().allocation_overflow);
}

TEST_CASE("Kernel delivers submitted orders after latency delay") {
  Kernel kernel;
  const MarketId market_id = kernel.add_market(std::make_unique<ContinuousMarket>());

  bool submitted = false;
  const AgentId agent_id = kernel.add_agent(std::make_unique<BuyerAgent>(market_id, &submitted));
  kernel.schedule(10, AgentWakeup{agent_id});
  kernel.run();

  KernelView view(kernel);
  const auto& market = dynamic_cast<const ContinuousMarket&>(view.market(market_id));
  REQUIRE(market.best_bid().has_value());
  CHECK(market.best_bid()->price == 100);
  CHECK(market.best_bid()->qty == 7);
  CHECK(submitted);
}

TEST_CASE("Order submitted at wakeup time t does not affect other agents observing at t") {
  Kernel kernel;
  const MarketId market_id = kernel.add_market(std::make_unique<ContinuousMarket>());

  std::uint64_t observer_qty = 999;
  const AgentId observer_id =
      kernel.add_agent(std::make_unique<ObserveOnlyAgent>(market_id, &observer_qty));
  const AgentId submitter_id = kernel.add_agent(
      std::make_unique<SubmitOnceAgent>(market_id, buy_msg(1, 100, 7)));

  kernel.schedule(10, AgentWakeup{submitter_id});
  kernel.schedule(10, AgentWakeup{observer_id});
  kernel.run();

  CHECK(observer_qty == 0);
  KernelView view(kernel);
  const auto& market = dynamic_cast<const ContinuousMarket&>(view.market(market_id));
  REQUIRE(market.best_bid().has_value());
  CHECK(market.best_bid()->qty == 7);
}

TEST_CASE("LatencyModel rejects zero agent delay") {
  LatencyModel model;
  CHECK_THROWS_AS(model.set_agent_delay(0, 0), std::invalid_argument);
}

TEST_CASE("ContinuousMarket with ProRata matches OrderBook for a sample sequence") {
  OrderBook book(std::make_unique<ProRata>());
  ContinuousMarket market(std::make_unique<ProRata>());

  const auto step = [&](Timestamp t, const OrderMessage& msg) {
    if (const auto* add = std::get_if<AddLimit>(&msg)) {
      const Order order{add->id, add->side, add->price, add->qty, t};
      book.add_limit(order, t);
      market.apply(msg, t);
      return;
    }
    if (const auto* cancel = std::get_if<CancelOrder>(&msg)) {
      book.cancel(cancel->id);
      market.apply(msg, t);
    }
  };

  step(1, sell_msg(1, 100, 10));
  step(2, sell_msg(2, 100, 10));
  step(3, buy_msg(3, 100, 15));

  CHECK(market.best_bid() == book.best_bid());
  CHECK(market.best_ask() == book.best_ask());
  CHECK(market.state_hash() == book.state_hash());
}
