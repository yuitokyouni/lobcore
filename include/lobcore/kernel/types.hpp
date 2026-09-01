#pragma once

#include <cstdint>

namespace lobcore {

using AgentId       = std::uint32_t;
using MarketId      = std::uint32_t;
using MarketTimerId = std::uint32_t;

// Phase 3 以降で要件 3 のメッセージ語彙に置き換える。
struct OrderMessage {};
struct NotificationPayload {};

}  // namespace lobcore
