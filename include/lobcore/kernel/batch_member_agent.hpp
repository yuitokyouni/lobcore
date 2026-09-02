#pragma once

#include <lobcore/kernel/agent.hpp>

namespace lobcore {

// バッチ所属エージェントのプレースホルダ。起床は Kernel がまとめて BatchStepFn に渡す。
class BatchMemberAgent final : public Agent {
 public:
  void on_wakeup(const KernelView&, AgentContext&) override {}

  void on_notification(const NotificationPayload&, const KernelView&,
                       AgentContext&) override {}
};

}  // namespace lobcore
