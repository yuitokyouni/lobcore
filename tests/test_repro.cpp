#include <catch2/catch_test_macros.hpp>

#include <lobcore/allocation.hpp>
#include <lobcore/event_log.hpp>
#include <lobcore/kernel/kernel.hpp>
#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/order_message.hpp>

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

using namespace lobcore;

namespace {

AddLimit buy_msg(OrderId id, Price price, Qty qty) {
  return AddLimit{id, Side::Buy, price, qty, 0};
}

AddLimit sell_msg(OrderId id, Price price, Qty qty) {
  return AddLimit{id, Side::Sell, price, qty, 0};
}

struct ScenarioResult {
  std::vector<LogRecord> log;
  std::uint64_t          market_hash = 0;
};

class MakerAgent final : public Agent {
 public:
  MakerAgent(MarketId market, OrderId order_id) : market_(market), order_id_(order_id) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    const Price offset = static_cast<Price>(ctx.rng(1).next_u64() % 3);
    ctx.submit(market_, sell_msg(order_id_, 100 + offset, 10));
    CHECK(ctx.schedule_wakeup(ctx.now() + 8));
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  order_id_;
  bool     done_ = false;
};

class TakerAgent final : public Agent {
 public:
  TakerAgent(MarketId market, OrderId first_id, OrderId second_id)
      : market_(market), first_id_(first_id), second_id_(second_id) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (step_ == 0) {
      const Price offset = static_cast<Price>(ctx.rng(1).next_u64() % 3);
      ctx.submit(market_, buy_msg(first_id_, 100 + offset, 4));
      CHECK(ctx.schedule_wakeup(ctx.now() + 6));
    } else if (step_ == 1) {
      ctx.submit(market_, buy_msg(second_id_, 101, 3));
    }
    ++step_;
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  first_id_;
  OrderId  second_id_;
  int      step_ = 0;
};

class CancelAgent final : public Agent {
 public:
  CancelAgent(MarketId market, OrderId target) : market_(market), target_(target) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    ctx.submit(market_, CancelOrder{target_});
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  target_;
  bool     done_ = false;
};

class DuplicateRejectAgent final : public Agent {
 public:
  DuplicateRejectAgent(MarketId market, OrderId duplicate_id)
      : market_(market), duplicate_id_(duplicate_id) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    ctx.submit(market_, buy_msg(duplicate_id_, 101, 2));
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  duplicate_id_;
  bool     done_ = false;
};

class OneShotSellAgent final : public Agent {
 public:
  OneShotSellAgent(MarketId market, OrderId order_id, Price price, Qty qty)
      : market_(market), order_id_(order_id), price_(price), qty_(qty) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    ctx.submit(market_, sell_msg(order_id_, price_, qty_));
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  order_id_;
  Price    price_;
  Qty      qty_;
  bool     done_ = false;
};

class OneShotBuyAgent final : public Agent {
 public:
  OneShotBuyAgent(MarketId market, OrderId order_id, Price price, Qty qty)
      : market_(market), order_id_(order_id), price_(price), qty_(qty) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_ = true;
    ctx.submit(market_, buy_msg(order_id_, price_, qty_));
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId market_;
  OrderId  order_id_;
  Price    price_;
  Qty      qty_;
  bool     done_ = false;
};

ScenarioResult run_scenario(const std::uint64_t master_seed,
                            std::unique_ptr<AllocationRule> rule = nullptr) {
  KernelConfig config;
  config.master_seed = master_seed;
  config.end_time    = 50;
  Kernel kernel(config);

  const MarketId market_id =
      kernel.add_market(std::make_unique<ContinuousMarket>(std::move(rule)));

  const AgentId maker_id   = kernel.add_agent(std::make_unique<MakerAgent>(market_id, 1000));
  const AgentId taker_id   = kernel.add_agent(std::make_unique<TakerAgent>(market_id, 2000, 2001));
  const AgentId cancel_id  = kernel.add_agent(std::make_unique<CancelAgent>(market_id, 1000));
  const AgentId reject_id  = kernel.add_agent(
      std::make_unique<DuplicateRejectAgent>(market_id, 2001));

  kernel.schedule(0, AgentWakeup{maker_id});
  kernel.schedule(0, AgentWakeup{taker_id});
  kernel.schedule(12, AgentWakeup{cancel_id});
  kernel.schedule(20, AgentWakeup{reject_id});
  kernel.run();

  KernelView view(kernel);
  const auto& market = dynamic_cast<const ContinuousMarket&>(view.market(market_id));
  return ScenarioResult{kernel.emitted_log(), market.state_hash()};
}

ScenarioResult run_overlap_scenario(const std::uint64_t              master_seed,
                                  std::unique_ptr<AllocationRule> rule) {
  KernelConfig config;
  config.master_seed = master_seed;
  config.end_time    = 10;
  Kernel kernel(config);

  const MarketId market_id =
      kernel.add_market(std::make_unique<ContinuousMarket>(std::move(rule)));

  const AgentId sell_a = kernel.add_agent(std::make_unique<OneShotSellAgent>(market_id, 100, 100, 10));
  const AgentId sell_b = kernel.add_agent(std::make_unique<OneShotSellAgent>(market_id, 101, 100, 10));
  const AgentId buyer  = kernel.add_agent(std::make_unique<OneShotBuyAgent>(market_id, 200, 100, 15));

  kernel.schedule(0, AgentWakeup{sell_a});
  kernel.schedule(0, AgentWakeup{sell_b});
  kernel.schedule(0, AgentWakeup{buyer});
  kernel.run();

  KernelView view(kernel);
  const auto& market = dynamic_cast<const ContinuousMarket&>(view.market(market_id));
  return ScenarioResult{kernel.emitted_log(), market.state_hash()};
}

struct CounterfactualScenarioResult {
  std::vector<std::uint64_t> rng0;
  std::vector<std::uint64_t> rng1;
  std::vector<std::uint64_t> rng2;
  std::vector<LogRecord>     log;
};

class SteppingRngAgent final : public Agent {
 public:
  SteppingRngAgent(MarketId market, std::vector<std::uint64_t>* rng_out, Timestamp stop_at,
                   OrderId order_id, Side side, Price price, Qty qty, Timestamp submit_at)
      : market_(market),
        rng_out_(rng_out),
        stop_at_(stop_at),
        order_id_(order_id),
        side_(side),
        price_(price),
        qty_(qty),
        submit_at_(submit_at) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    rng_out_->push_back(ctx.rng(0).next_u64());
    if (!submitted_ && submit_at_ >= 0 && ctx.now() == submit_at_) {
      submitted_ = true;
      if (side_ == Side::Buy) {
        ctx.submit(market_, buy_msg(order_id_, price_, qty_));
      } else {
        ctx.submit(market_, sell_msg(order_id_, price_, qty_));
      }
    }
    if (ctx.now() < stop_at_) {
      CHECK(ctx.schedule_wakeup(ctx.now() + 1));
    }
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  MarketId                   market_;
  std::vector<std::uint64_t>* rng_out_;
  Timestamp                  stop_at_;
  OrderId                    order_id_;
  Side                       side_;
  Price                      price_;
  Qty                        qty_;
  Timestamp                  submit_at_;
  bool                       submitted_ = false;
};

CounterfactualScenarioResult run_counterfactual_scenario(const bool suppress_middle) {
  KernelConfig config;
  config.master_seed = 4242;
  config.end_time    = 10;
  Kernel kernel(config);

  if (suppress_middle) {
    kernel.suppress_agent(1);
  }

  const MarketId market_id = kernel.add_market(std::make_unique<ContinuousMarket>());

  CounterfactualScenarioResult result;

  kernel.add_agent(std::make_unique<SteppingRngAgent>(
      market_id, &result.rng0, 10, 1000, Side::Sell, 100, 5, 2));
  kernel.add_agent(std::make_unique<SteppingRngAgent>(
      market_id, &result.rng1, 10, 2000, Side::Buy, 50, 3, 5));
  kernel.add_agent(
      std::make_unique<SteppingRngAgent>(market_id, &result.rng2, 10, 3000, Side::Buy, 50, 1, -1));

  kernel.schedule(0, AgentWakeup{0});
  kernel.schedule(0, AgentWakeup{1});
  kernel.schedule(0, AgentWakeup{2});
  kernel.run();

  result.log = kernel.emitted_log();
  return result;
}

bool log_record_from_order(const LogRecord& rec, const OrderId order_id) {
  if (rec.order_id == order_id || rec.maker_id == order_id) {
    return true;
  }
  return false;
}

std::vector<LogRecord> log_without_order(const std::vector<LogRecord>& log, const OrderId order_id) {
  std::vector<LogRecord> out;
  out.reserve(log.size());
  for (const LogRecord& rec : log) {
    if (!log_record_from_order(rec, order_id)) {
      out.push_back(rec);
    }
  }
  return out;
}

std::vector<LogRecord> log_before_time(const std::vector<LogRecord>& log, const Timestamp before) {
  std::vector<LogRecord> out;
  out.reserve(log.size());
  for (const LogRecord& rec : log) {
    if (rec.received_at < before) {
      out.push_back(rec);
    }
  }
  return out;
}

TEST_CASE("suppress_agent preserves all agents' rng streams") {
  const auto factual  = run_counterfactual_scenario(false);
  const auto baseline = run_counterfactual_scenario(true);

  CHECK(factual.rng0 == baseline.rng0);
  CHECK(factual.rng1 == baseline.rng1);
  CHECK(factual.rng2 == baseline.rng2);

  const auto factual_prefix  = log_before_time(factual.log, 5);
  const auto baseline_prefix = log_before_time(baseline.log, 5);
  CHECK(factual_prefix == baseline_prefix);

  const auto factual_filtered = log_without_order(factual.log, 2000);
  CHECK(factual_filtered == baseline.log);
}

}  // namespace

TEST_CASE("same seed reproduces identical log and market state") {
  const auto first  = run_scenario(12345);
  const auto second = run_scenario(12345);

  REQUIRE_FALSE(first.log.empty());
  CHECK(log_hash(first.log) == log_hash(second.log));
  CHECK(first.market_hash == second.market_hash);
  CHECK(first.log == second.log);
}

TEST_CASE("ProRata market reproduces identical log and market state for same seed") {
  const auto first  = run_scenario(12345, std::make_unique<ProRata>());
  const auto second = run_scenario(12345, std::make_unique<ProRata>());

  REQUIRE_FALSE(first.log.empty());
  CHECK(log_hash(first.log) == log_hash(second.log));
  CHECK(first.market_hash == second.market_hash);
  CHECK(first.log == second.log);
}

TEST_CASE("different master seed yields different log hash") {
  const auto a = run_scenario(1);
  const auto b = run_scenario(2);

  REQUIRE_FALSE(a.log.empty());
  CHECK(log_hash(a.log) != log_hash(b.log));
}

TEST_CASE("allocation rule changes kernel log for same seed") {
  const auto fifo    = run_overlap_scenario(42, std::make_unique<PriceTimePriority>());
  const auto pro_rata = run_overlap_scenario(42, std::make_unique<ProRata>());

  REQUIRE_FALSE(fifo.log.empty());
  REQUIRE_FALSE(pro_rata.log.empty());
  CHECK(log_hash(fifo.log) != log_hash(pro_rata.log));
  CHECK(fifo.market_hash != pro_rata.market_hash);
}

TEST_CASE("Stage 2 replay reproduces Stage 4 kernel market state") {
  const auto result = run_scenario(999);
  REQUIRE_FALSE(result.log.empty());

  const OrderBook replayed = replay(result.log);
  CHECK(replayed.state_hash() == result.market_hash);
  CHECK(replayed.locations_consistent());
}

TEST_CASE("Stage 2 replay reproduces ProRata kernel market state") {
  const auto result = run_scenario(999, std::make_unique<ProRata>());
  REQUIRE_FALSE(result.log.empty());

  const OrderBook replayed = replay(result.log, std::make_unique<ProRata>());
  CHECK(replayed.state_hash() == result.market_hash);
  CHECK(replayed.locations_consistent());
  CHECK(replayed.rejects().allocation_overflow == 0);
}

TEST_CASE("Kernel order delivery emits Add Fill and Cancel records") {
  Kernel kernel;
  const MarketId market_id = kernel.add_market(std::make_unique<ContinuousMarket>());

  kernel.schedule(1, OrderDelivery{0, market_id, sell_msg(1, 100, 10)});
  kernel.schedule(2, OrderDelivery{0, market_id, buy_msg(2, 100, 4)});
  kernel.schedule(3, OrderDelivery{0, market_id, CancelOrder{1}});
  kernel.run();

  const auto& log = kernel.emitted_log();
  REQUIRE(log.size() >= 4);
  CHECK(log[0].kind == EventKind::Add);
  CHECK(log[1].kind == EventKind::Add);
  CHECK(log[2].kind == EventKind::Fill);
  CHECK(log[3].kind == EventKind::Cancel);
}

TEST_CASE("Kernel ProRata market logs allocation overflow reject") {
  Kernel kernel;
  const MarketId market_id =
      kernel.add_market(std::make_unique<ContinuousMarket>(std::make_unique<ProRata>()));
  const Qty large = std::numeric_limits<Qty>::max() / 2 + 1;

  kernel.schedule(1, OrderDelivery{0, market_id, sell_msg(1, 100, large)});
  kernel.schedule(2, OrderDelivery{0, market_id, buy_msg(2, 100, 2)});
  kernel.run();

  const auto& log = kernel.emitted_log();
  REQUIRE(log.size() == 2);
  CHECK(log[0].kind == EventKind::Add);
  CHECK(log[1].kind == EventKind::Reject);
  CHECK(log[1].reason == RejectReason::AllocationOverflow);

  KernelView view(kernel);
  const auto& market = dynamic_cast<const ContinuousMarket&>(view.market(market_id));
  CHECK(market.rejects().allocation_overflow == 1);

  const OrderBook replayed = replay(log, std::make_unique<ProRata>());
  CHECK(replayed.state_hash() == market.state_hash());
  CHECK(replayed.rejects().allocation_overflow == 1);
}
