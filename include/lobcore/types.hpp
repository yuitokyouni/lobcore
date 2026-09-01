#pragma once

#include <cstdint>

namespace lobcore {

// 価格は「ティック数」の整数で持つ。double は使わない。
//   - 比較 (==, <) が厳密に決まる
//   - 決定論的リプレイ (Stage 2) で丸め誤差の問題が起きない
//   - 実価格 = ticks * tick_size は境界で一度だけ変換する
using Price     = std::int64_t;
using Qty       = std::int64_t;
using Timestamp = std::int64_t;

// 注文 ID は呼び出し側 (将来はシミュレータ核) が付番する。
using OrderId = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

inline Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

}  // namespace lobcore
