#include <catch2/catch_test_macros.hpp>

#include <lobcore/kernel/agent.hpp>
#include <lobcore/kernel/kernel.hpp>

#include <cstdint>
#include <memory>
#include <vector>

using namespace lobcore;

namespace {

class RecordingAgent final : public Agent {
 public:
  explicit RecordingAgent(std::vector<std::uint64_t>* out) : out_(out) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    out_->push_back(ctx.rng(0).next_u64());
    if (ctx.now() < stop_at_) {
      CHECK(ctx.schedule_wakeup(ctx.now() + 1));
    }
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

  Timestamp stop_at_ = 3;

 private:
  std::vector<std::uint64_t>* out_;
};

class RejectWakeupAgent final : public Agent {
 public:
  explicit RejectWakeupAgent(bool* same_time_rejected, bool* future_accepted)
      : same_time_rejected_(same_time_rejected), future_accepted_(future_accepted) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    if (done_) {
      return;
    }
    done_                = true;
    *same_time_rejected_ = !ctx.schedule_wakeup(ctx.now());
    *future_accepted_    = ctx.schedule_wakeup(ctx.now() + 1);
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  bool* same_time_rejected_;
  bool* future_accepted_;
  bool  done_ = false;
};

class ComponentProbeAgent final : public Agent {
 public:
  ComponentProbeAgent(std::uint64_t* c0_first, std::uint64_t* c1_first) : c0_first_(c0_first), c1_first_(c1_first) {}

  void on_wakeup(const KernelView& /*view*/, AgentContext& ctx) override {
    *c0_first_ = ctx.rng(0).next_u64();
    *c1_first_ = ctx.rng(1).next_u64();
  }

  void on_notification(const NotificationPayload& /*notification*/,
                       const KernelView& /*view*/,
                       AgentContext& /*ctx*/) override {}

 private:
  std::uint64_t* c0_first_;
  std::uint64_t* c1_first_;
};

void run_two_agent_recording(std::uint64_t master_seed,
                             Timestamp      end_time,
                             std::vector<std::uint64_t>& agent0,
                             std::vector<std::uint64_t>& agent1,
                             bool include_third_agent,
                             std::vector<std::uint64_t>* agent2 = nullptr) {
  KernelConfig config;
  config.master_seed = master_seed;
  config.end_time    = end_time;
  Kernel kernel(config);

  kernel.add_agent(std::make_unique<RecordingAgent>(&agent0));
  kernel.add_agent(std::make_unique<RecordingAgent>(&agent1));
  if (include_third_agent) {
    REQUIRE(agent2 != nullptr);
    kernel.add_agent(std::make_unique<RecordingAgent>(agent2));
    kernel.schedule(0, AgentWakeup{2});
  }

  kernel.schedule(0, AgentWakeup{0});
  kernel.schedule(0, AgentWakeup{1});
  kernel.run();
}

}  // namespace

TEST_CASE("AgentContext rejects schedule_wakeup at current time") {
  bool same_time_rejected = false;
  bool future_accepted    = false;

  Kernel kernel;
  const AgentId id = kernel.add_agent(
      std::make_unique<RejectWakeupAgent>(&same_time_rejected, &future_accepted));
  kernel.schedule(5, AgentWakeup{id});
  kernel.run();

  CHECK(same_time_rejected);
  CHECK(future_accepted);
}

TEST_CASE("Rng streams are reproducible for the same master seed") {
  std::vector<std::uint64_t> first0;
  std::vector<std::uint64_t> first1;
  run_two_agent_recording(42, 3, first0, first1, false);

  std::vector<std::uint64_t> second0;
  std::vector<std::uint64_t> second1;
  run_two_agent_recording(42, 3, second0, second1, false);

  CHECK(first0 == second0);
  CHECK(first1 == second1);
}

TEST_CASE("Adding an agent does not change existing agents' rng streams") {
  std::vector<std::uint64_t> baseline0;
  std::vector<std::uint64_t> baseline1;
  run_two_agent_recording(99, 3, baseline0, baseline1, false);

  std::vector<std::uint64_t> with_extra0;
  std::vector<std::uint64_t> with_extra1;
  std::vector<std::uint64_t> with_extra2;
  run_two_agent_recording(99, 3, with_extra0, with_extra1, true, &with_extra2);

  CHECK(baseline0 == with_extra0);
  CHECK(baseline1 == with_extra1);
  REQUIRE_FALSE(with_extra2.empty());
}

TEST_CASE("Rng components are isolated within an agent") {
  std::uint64_t c0 = 0;
  std::uint64_t c1 = 0;

  KernelConfig config;
  config.master_seed = 7;
  Kernel kernel(config);
  const AgentId id =
      kernel.add_agent(std::make_unique<ComponentProbeAgent>(&c0, &c1));
  kernel.schedule(1, AgentWakeup{id});
  kernel.run();

  CHECK(c0 != 0);
  CHECK(c1 != 0);
  CHECK(c0 != c1);
}

TEST_CASE("make_rng derives distinct streams per agent id") {
  const Rng a = make_rng(1, StreamKey{.agent_id = 0, .component_id = 0});
  const Rng b = make_rng(1, StreamKey{.agent_id = 1, .component_id = 0});

  Rng copy_a = a;
  Rng copy_b = b;
  CHECK(copy_a.next_u64() != copy_b.next_u64());
}
