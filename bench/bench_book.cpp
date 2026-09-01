// Stage 3 用。-DLOBCORE_BUILD_BENCH=ON、必ず Release で走らせる。
// 測るのは注文 1 件あたりの処理時間のみ。
#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include <lobcore/book.hpp>

namespace {

constexpr int kDeepLevels     = 1000;
constexpr int kOrdersPerLevel = 10;

lobcore::OrderBook make_deep_book(lobcore::OrderId& next_id, lobcore::Timestamp& next_ts) {
  lobcore::OrderBook book;
  // bids: 1000..1 (best=1000), asks: 1001..2000 (best=1001)
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

// 空の板に指値を積む (約定なし)。
static void BM_RestOnEmpty(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    lobcore::OrderBook book;
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    state.ResumeTiming();

    for (int i = 0; i < 1000; ++i) {
      const lobcore::Timestamp ts = next_ts++;
      auto trades =
          book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, 100, 1, ts}, ts);
      benchmark::DoNotOptimize(trades);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 1000);
}
BENCHMARK(BM_RestOnEmpty);

// 深い板へ非交差の指値を挿入する。
static void BM_RestOnDeepBook(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    lobcore::OrderBook book    = make_deep_book(next_id, next_ts);
    state.ResumeTiming();

    // best ask = 1001 なので Buy @ 1000 は交差しない。
    for (int i = 0; i < 1000; ++i) {
      const lobcore::Timestamp ts = next_ts++;
      auto trades =
          book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, 1000, 1, ts}, ts);
      benchmark::DoNotOptimize(trades);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 1000);
}
BENCHMARK(BM_RestOnDeepBook);

// 1 注文が 1 レベルで完結する交差 (全量約定、スイープなし)。
static void BM_CrossSingleLevel(benchmark::State& state) {
  std::int64_t total_orders = 0;
  std::int64_t total_fills  = 0;

  for (auto _ : state) {
    state.PauseTiming();
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    lobcore::OrderBook book    = make_deep_book(next_id, next_ts);
    // best ask (1001) を厚くする: 200 注文 × qty=5 = 1000 を 1 レベルで賄う。
    // make_deep_book 時点で既に 10 本あるので、追加で 990 本積む。
    for (int n = 0; n < 990; ++n) {
      const lobcore::Timestamp ts = next_ts++;
      book.add_limit(lobcore::Order{next_id++, lobcore::Side::Sell, 1001, 1, ts}, ts);
    }
    state.ResumeTiming();

    for (int i = 0; i < 200; ++i) {
      const lobcore::Timestamp ts = next_ts++;
      auto trades =
          book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, 1001, 5, ts}, ts);
      total_fills += static_cast<std::int64_t>(trades.size());
      ++total_orders;
      benchmark::DoNotOptimize(trades);
    }
  }
  state.SetItemsProcessed(total_orders);
  state.counters["avg_fills_per_order"] =
      total_orders == 0 ? 0.0
                        : static_cast<double>(total_fills) / static_cast<double>(total_orders);
}
BENCHMARK(BM_CrossSingleLevel);

// 複数レベルをスイープする交差 (全量約定)。
static void BM_CrossSweep(benchmark::State& state) {
  std::int64_t total_orders = 0;
  std::int64_t total_fills  = 0;

  for (auto _ : state) {
    state.PauseTiming();
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    lobcore::OrderBook book    = make_deep_book(next_id, next_ts);
    state.ResumeTiming();

    // 各 ask レベルに qty=1×10。Buy qty=50 @ 1005 は 5 レベルをスイープし全量約定。
    // 深い板の ask 在庫は十分あるので、1 イテレーションで複数スイープ可能。
    for (int i = 0; i < 20; ++i) {
      const lobcore::Timestamp ts = next_ts++;
      auto trades =
          book.add_limit(lobcore::Order{next_id++, lobcore::Side::Buy, 2000, 50, ts}, ts);
      total_fills += static_cast<std::int64_t>(trades.size());
      ++total_orders;
      benchmark::DoNotOptimize(trades);
    }
  }
  state.SetItemsProcessed(total_orders);
  state.counters["avg_fills_per_order"] =
      total_orders == 0 ? 0.0
                        : static_cast<double>(total_fills) / static_cast<double>(total_orders);
}
BENCHMARK(BM_CrossSweep);

// 深い板でランダムな注文を cancel する。
// 板の再構築と shuffle は PauseTiming 中のみ。
static void BM_CancelDeepBook(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    lobcore::OrderId   next_id = 1;
    lobcore::Timestamp next_ts = 1;
    lobcore::OrderBook book    = make_deep_book(next_id, next_ts);

    std::vector<lobcore::OrderId> ids;
    ids.reserve(static_cast<std::size_t>(next_id - 1));
    for (lobcore::OrderId id = 1; id < next_id; ++id) {
      ids.push_back(id);
    }
    std::mt19937_64 rng(42);
    std::shuffle(ids.begin(), ids.end(), rng);
    constexpr std::size_t kCancelCount = 1000;
    state.ResumeTiming();

    for (std::size_t i = 0; i < kCancelCount; ++i) {
      bool ok = book.cancel(ids[i]);
      benchmark::DoNotOptimize(ok);
    }
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 1000);
}
BENCHMARK(BM_CancelDeepBook);

}  // namespace

BENCHMARK_MAIN();
