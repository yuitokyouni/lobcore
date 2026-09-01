// CrossSweep プロファイル用。callgrind の計測は交差注文だけに限定する。
#include <cstdint>

#include <valgrind/callgrind.h>

#include <lobcore/book.hpp>

namespace {

constexpr int kDeepLevels     = 1000;
constexpr int kOrdersPerLevel = 10;

lobcore::OrderBook make_deep_book(lobcore::OrderId& next_id, lobcore::Timestamp& next_ts) {
  lobcore::OrderBook book;
  for (int level = 0; level < kDeepLevels; ++level) {
    const lobcore::Price bid_px = 1000 - level;
    const lobcore::Price ask_px = 1001 + level;
    for (int n = 0; n < kOrdersPerLevel; ++n) {
      const lobcore::Timestamp ts = next_ts++;
      book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, bid_px, 1, ts}, ts);
      const lobcore::Timestamp ts2 = next_ts++;
      book.add_limit(lobcore::Order{next_id++, lobcore::Side::Sell, ask_px, 1, ts2}, ts2);
    }
  }
  return book;
}

}  // namespace

int main() {
  volatile std::uint64_t sink = 0;

  // 計測は CrossSweep 注文のみ（板の構築は CALLGRIND_START の外）
  for (int rep = 0; rep < 50; ++rep) {
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    // 計測区間外で板を構築（move 代入のコピーは計測に含めない）
    lobcore::OrderBook book = make_deep_book(next_id, next_ts);

    CALLGRIND_START_INSTRUMENTATION;
    for (int i = 0; i < 20; ++i) {
      const lobcore::Timestamp ts = next_ts++;
      auto trades =
          book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, 2000, 50, ts}, ts);
      sink += trades.size();
    }
    CALLGRIND_STOP_INSTRUMENTATION;
  }

  return static_cast<int>(sink & 0xFF);
}
