#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <lobcore/kernel/batch.hpp>
#include <lobcore/kernel/batch_member_agent.hpp>
#include <lobcore/kernel/kernel.hpp>
#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/order_message.hpp>

using namespace lobcore;

namespace {

class RecordingAgent final : public Agent {
 public:
  std::vector<Timestamp>* wake_log = nullptr;
  AgentId*                last_id  = nullptr;

  void on_wakeup(const KernelView& view, AgentContext& ctx) override {
    if (wake_log) {
      wake_log->push_back(view.now());
    }
    if (last_id) {
      *last_id = ctx.id();
    }
  }

  void on_notification(const NotificationPayload&, const KernelView&, AgentContext&) override {}
};

}  // namespace

TEST_CASE("Kernel collects same-time AgentWakeups before dispatching") {
  Kernel kernel;
  std::vector<Timestamp> log_a;
  std::vector<Timestamp> log_b;

  auto a = std::make_unique<RecordingAgent>();
  a->wake_log = &log_a;
  auto b = std::make_unique<RecordingAgent>();
  b->wake_log = &log_b;
  const AgentId id_a = kernel.add_agent(std::move(a));
  const AgentId id_b = kernel.add_agent(std::move(b));

  kernel.schedule(10, AgentWakeup{id_b});
  kernel.schedule(10, AgentWakeup{id_a});
  kernel.run();

  REQUIRE(log_a.size() == 1);
  REQUIRE(log_b.size() == 1);
  CHECK(log_a[0] == 10);
  CHECK(log_b[0] == 10);
  // seq 順: 先に schedule した id_b が先
  const auto& processed = kernel.processed_events();
  REQUIRE(processed.size() == 2);
  CHECK(std::get<AgentWakeup>(processed[0].body).agent == id_b);
  CHECK(std::get<AgentWakeup>(processed[1].body).agent == id_a);
}

TEST_CASE("batch step receives all same-time members in one call") {
  Kernel kernel;
  int    step_calls = 0;
  std::vector<AgentId> seen;

  const auto ids = kernel.add_batch_agents(
      [&](const BatchObservation& obs) {
        ++step_calls;
        seen = obs.agent_ids;
        BatchAction act;
        act.next_wakeups.assign(obs.agent_ids.size(), Timestamp{0});
        return act;
      },
      3);

  REQUIRE(ids.size() == 3);
  kernel.schedule(5, AgentWakeup{ids[2]});
  kernel.schedule(5, AgentWakeup{ids[0]});
  kernel.schedule(5, AgentWakeup{ids[1]});
  kernel.run();

  CHECK(step_calls == 1);
  REQUIRE(seen.size() == 3);
  CHECK(seen[0] == ids[2]);
  CHECK(seen[1] == ids[0]);
  CHECK(seen[2] == ids[1]);
}

TEST_CASE("batch action schedules wakeups and submits orders") {
  Kernel kernel(KernelConfig{.end_time = 100, .max_events = std::nullopt, .master_seed = 1});
  const MarketId mid = kernel.add_market(std::make_unique<ContinuousMarket>());

  const auto ids = kernel.add_batch_agents(
      [&](const BatchObservation& obs) {
        BatchAction act;
        act.next_wakeups.assign(obs.agent_ids.size(), Timestamp{0});
        if (obs.now == 1) {
          act.next_wakeups[0] = 10;
          OrderSubmission sub;
          sub.agent_id  = obs.agent_ids[0];
          sub.market_id = mid;
          sub.msg       = AddLimit{.id = 1, .side = Side::Buy, .price = 100, .qty = 1};
          act.orders.push_back(sub);
        }
        return act;
      },
      1);

  kernel.schedule(1, AgentWakeup{ids[0]});
  kernel.run();

  CHECK(kernel.market(mid).best_bid().has_value());
  CHECK(kernel.market(mid).best_bid()->price == 100);
}

TEST_CASE("batch rejects invalid wakeup and foreign orders") {
  Kernel kernel;
  const auto ids = kernel.add_batch_agents(
      [&](const BatchObservation& obs) {
        BatchAction act;
        act.next_wakeups.assign(obs.agent_ids.size(), Timestamp{0});
        act.next_wakeups[0] = obs.now;  // invalid
        OrderSubmission sub;
        sub.agent_id  = 999;  // not awake
        sub.market_id = 0;
        sub.msg       = AddLimit{.id = 1, .side = Side::Buy, .price = 100, .qty = 1};
        act.orders.push_back(sub);
        return act;
      },
      1);

  kernel.schedule(3, AgentWakeup{ids[0]});
  kernel.run();

  CHECK(kernel.batch_rejects().invalid_wakeup == 1);
  CHECK(kernel.batch_rejects().order_from_sleeping_agent == 1);
}

TEST_CASE("batch next_wakeups size mismatch throws") {
  Kernel kernel;
  const auto ids = kernel.add_batch_agents(
      [&](const BatchObservation&) {
        BatchAction act;
        act.next_wakeups = {0, 0};  // wrong size
        return act;
      },
      1);

  kernel.schedule(1, AgentWakeup{ids[0]});
  CHECK_THROWS_AS(kernel.run(), std::invalid_argument);
}

TEST_CASE("native agents still wake one-by-one alongside batch") {
  Kernel kernel;
  int    native_wakes = 0;
  int    batch_calls  = 0;

  auto native = std::make_unique<RecordingAgent>();
  // RecordingAgent needs a hook — use custom via batch count only
  class CountAgent final : public Agent {
   public:
    int* counter = nullptr;
    void on_wakeup(const KernelView&, AgentContext&) override {
      if (counter) {
        ++(*counter);
      }
    }
    void on_notification(const NotificationPayload&, const KernelView&, AgentContext&) override {}
  };
  auto na = std::make_unique<CountAgent>();
  na->counter = &native_wakes;
  const AgentId nid = kernel.add_agent(std::move(na));

  const auto bids = kernel.add_batch_agents(
      [&](const BatchObservation& obs) {
        ++batch_calls;
        BatchAction act;
        act.next_wakeups.assign(obs.agent_ids.size(), Timestamp{0});
        return act;
      },
      2);

  kernel.schedule(7, AgentWakeup{bids[0]});
  kernel.schedule(7, AgentWakeup{nid});
  kernel.schedule(7, AgentWakeup{bids[1]});
  kernel.run();

  CHECK(native_wakes == 1);
  CHECK(batch_calls == 1);
}
