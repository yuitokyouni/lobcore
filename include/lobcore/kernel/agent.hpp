#pragma once

#include <lobcore/kernel/market.hpp>
#include <lobcore/kernel/rng.hpp>
#include <lobcore/kernel/types.hpp>
#include <lobcore/types.hpp>

namespace lobcore {

class Kernel;

class KernelView {
 public:
  explicit KernelView(const Kernel& kernel) : kernel_(kernel) {}

  [[nodiscard]] Timestamp now() const noexcept;
  [[nodiscard]] const Market& market(MarketId id) const;
  [[nodiscard]] std::size_t   market_count() const noexcept;

 private:
  const Kernel& kernel_;
};

class AgentContext {
 public:
  AgentContext(Kernel& kernel, AgentId id) : kernel_(kernel), id_(id) {}

  [[nodiscard]] Timestamp now() const noexcept;
  [[nodiscard]] AgentId   id() const noexcept { return id_; }

  [[nodiscard]] Rng& rng(ComponentId component);

  void submit(MarketId to, const OrderMessage& msg);

  // t > now() のときだけ true。false ならヒープに積まない。
  [[nodiscard]] bool schedule_wakeup(Timestamp t);

 private:
  Kernel& kernel_;
  AgentId id_;
};

class Agent {
 public:
  virtual ~Agent() = default;

  virtual void on_wakeup(const KernelView& view, AgentContext& ctx) = 0;

  virtual void on_notification(const NotificationPayload& notification,
                               const KernelView&           view,
                               AgentContext&               ctx) = 0;
};

}  // namespace lobcore
