// Stage 3 用。-DLOBCORE_BUILD_BENCH=ON、必ず Release で走らせる。
#include <benchmark/benchmark.h>

#include <random>

#include <lobcore/book.hpp>

namespace {

// 買い/売りを半々、価格は狭い帯域で一様。
// 最初の計測にはこれで十分。現実の注文流に寄せるのは後回し。
static void BM_AddLimit(benchmark::State& state) {
  std::mt19937_64 rng(42);  // 固定シード: 実行ごとに同じ注文列
  std::uniform_int_distribution<lobcore::Price> price(90, 110);
  std::uniform_int_distribution<lobcore::Qty> qty(1, 100);
  std::bernoulli_distribution is_buy(0.5);

  lobcore::OrderBook book;
  lobcore::OrderId   next_id = 1;
  lobcore::Timestamp next_ts = 1;

  for (auto _ : state) {
    const lobcore::Timestamp ts = next_ts++;
    lobcore::Order           o{next_id++,
                     is_buy(rng) ? lobcore::Side::Buy : lobcore::Side::Sell, price(rng),
                     qty(rng), ts};
    auto trades = book.add_limit(o, ts);
    benchmark::DoNotOptimize(trades);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_AddLimit);

}  // namespace

BENCHMARK_MAIN();
