# lobcore

**EN** | [JA](README.md)

**An experimental foundation for studying how agents' decisions appear in the market through orders and trades.**

lobcore provides a C++20 limit order book (LOB) matching engine, a discrete-event market simulation kernel, and a Python interface. You can write agent behavior in Python, process order arrivals, fills, and cancellations in C++, and retrieve the results as an event log.

It supports rerunning experiments with the same configuration and seed, as well as comparisons that suppress orders from specific agents. Investor behavior models, learning, calibration with real data, and study-specific analysis are built in the experiments that use lobcore.

> **For users of existing experiments: Python random-stream retrieval was corrected on 2026-09-17.**
> See the [correction notes](docs/python-rng-correction.md) for the issue where retrieving `ctx.rng()` / `rng_for()` / `sentinel_rng()` again reset the random sequence to the beginning, and for which past experiments need to be rerun.

## Using lobcore in research

Agent decision-making and market order processing are handled separately.

```text
Experiment                                 lobcore                         Experiment
Information, price history, holdings → submit/cancel orders → arrivals, matching → log aggregation, hypothesis testing
Rules, trained models                      Market rules, event processing  Price, fill, and liquidity analysis
```

For example, it can serve as a foundation for experiments that:

- Compare market responses with and without a participant's orders under the same background configuration.
- Examine how fills and price formation change under price-time priority and pro-rata allocation.
- Replace investors' decision rules and examine the relationship between buy-order replenishment or cancellation and price changes.

Research that connects decision models such as SG, neural networks, or LLMs is an extension use case. These agents, cash/inventory/profit-and-loss management, and markets calibrated to real data are not included. Market statistical properties and suitability for the hypotheses under study must be validated in each experiment.

## Implemented features

| Component | Description |
|---|---|
| Order book | Limit orders, partial fills, cancellations, best quotes, and remaining quantity for each order. Prices and quantities are integers |
| Allocation rules | Price-time priority and quantity-proportional allocation (pro-rata) at the same price |
| Simulation kernel | Integer-time event queue, agent wakeups, delayed order arrivals, and an interface for timed market events |
| Python agents | `Agent.on_wakeup(view, ctx)`, order submission, cancellation, and scheduling the next wakeup. Python agents waking at the same time are processed in a batch |
| Random numbers | Independent streams derived from a master seed and agent/component IDs |
| Event log | Orders, fills, cancellations, and rejections recorded in fixed-length records. Retrieved and saved as NumPy structured arrays in Python |
| Reproducibility and comparison | C++ log replay and state hashes, byte-level log comparison, suppression of orders from specified agents, and a helper for paired runs |
| Validation | Differential tests against a reference implementation using random order sequences, C++/Python tests, and optional benchmarks |

The main components of Stages 1–5 and the comparison-experiment infrastructure for Stage 6 Phase 0 are implemented. The design documents also include future extensions; refer to this README and the public API for the current scope.

## Getting started with Python

You need Python 3.10 or later, a C++20-compatible compiler, and CMake 3.21 or later. NumPy and the build dependencies scikit-build-core and pybind11 are fetched during installation. A network connection is required for the initial dependency download.

Run the following from the repository root.

```sh
git clone https://github.com/yuitokyouni/lobcore.git
cd lobcore
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e "./python[test]"
python -m pytest python/tests -q
```

To activate the virtual environment in Windows PowerShell, use `.venv\Scripts\Activate.ps1`. Current Python CI runs on Ubuntu with Python 3.12.

### Minimal fill example

Two agents each submit an order once. Prices are integer ticks, and quantities are integer units.

```python
from lobcore import Agent, Experiment


class PlaceOnce(Agent):
    def __init__(self, side, price, qty):
        self.side = side
        self.price = price
        self.qty = qty

    def on_wakeup(self, view, ctx):
        ctx.submit(0, self.side, self.price, self.qty)
        # No next wakeup is scheduled, so the agent submits only once.


result = Experiment(
    seed=42,
    agents=[PlaceOnce("sell", 101, 10), PlaceOnce("buy", 101, 4)],
    end_time=3,
    rule="price_time",  # Can be changed to "pro_rata"
    strict=True,
).run()

fills = result.log[result.log["kind"] == 1]  # 1 = Fill
print(fills[["price", "qty"]])  # [(101, 4)]
```

The first wakeup is at time 1, and the default order-arrival delay is 1. In this example, the sell order arrives first at time 2, followed by the buy order, resulting in a fill of quantity 4 at price 101. The unfilled sell quantity of 6 remains in the book.

Use `view.market(0)` to inspect the best quotes and remaining order quantities, `ctx.cancel()` to cancel an order, `ctx.schedule_wakeup()` to schedule the next action time, and `ctx.rng(component)` to select a random stream. `strict=True` propagates exceptions from agents to the caller. With the default `False`, the agent that raises an exception is stopped and the run continues.

### Comparing runs with suppressed orders

Using `PlaceOnce` above, suppress only the buy agent's orders. Agent IDs are assigned from 0 in registration order.

```python
def make_experiment():
    # Create new agents for each run, including models with history or inventory.
    return Experiment(
        seed=42,
        agents=[PlaceOnce("sell", 101, 10), PlaceOnce("buy", 101, 4)],
        end_time=3,
        strict=True,
    )


factual = make_experiment().run()
baseline = make_experiment().run(suppress_agent_ids=[1])

print((factual.log["kind"] == 1).sum())   # 1
print((baseline.log["kind"] == 1).sum())  # 0
```

The suppressed agent still wakes up normally and can use random numbers, but its order submissions are not delivered to the market. This operation suppresses submissions from specified agents; it does not remove a single specified order.

`Experiment.run_pair(suppress_agent_ids=[...])` is also available, but it reuses the same Python agent/step objects in both runs. For models with history, inventory, or learning state, run each experiment with fresh objects as shown above.

Separating random streams does not automatically align every random-number call or action time after an intervention. In counterfactual experiments, the experiment must define matching exogenous series, initial states, and pre-intervention logs, as well as the scope of changes after the intervention. When using external services such as LLMs, the experiment also manages the conditions for reproducibility, including saving and reusing responses.

## Building and testing C++

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Catch2 is fetched on the first build. ASan/UBSan are enabled by default in GCC/Clang Debug builds. C++ CI runs on Ubuntu and macOS.

For a multi-configuration build with Visual Studio 2022:

```sh
cmake -S . -B build-vs -G "Visual Studio 17 2022"
cmake --build build-vs --config Debug
ctest --test-dir build-vs -C Debug --output-on-failure
```

### Benchmarks

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release -DLOBCORE_BUILD_BENCH=ON
cmake --build build-rel
./build-rel/bench/lobcore_bench
```

Google Benchmark is fetched and built. Use Release builds for performance comparisons, and report the compiler, execution environment, and workload alongside the results. The measurement design is documented in [Stage 3](docs/stage3-benchmark.md).

| CMake option | Default | Purpose |
|---|---|---|
| `LOBCORE_BUILD_TESTS` | `ON` | C++ tests |
| `LOBCORE_BUILD_BENCH` | `OFF` | Benchmarks |
| `LOBCORE_BUILD_PYTHON` | `OFF` | Python extension |
| `LOBCORE_SANITIZE` | `ON` | GCC/Clang Debug sanitizers |

Installing the Python package builds with the Python extension enabled and C++ tests, benchmarks, and sanitizers disabled.

## Model assumptions and current limitations

- **Matching:** The fill price is the limit price of the resting order (maker). Time priority is determined by arrival-processing order, without using a wall clock. Pro-rata allocates quantities in proportion to remaining quantities at the same price; rounding uses the largest-remainder method, with ties broken by arrival order.
- **Time:** Simulation time is an integer. Its mapping to seconds, milliseconds, or other units is defined by the experiment. Agents waking at the same time observe the same book state, and orders arrive after a positive delay. Per-agent delay settings are available in the C++ API but are not exposed in the current Python API.
- **Orders and markets:** The implemented order messages are limit orders and cancellations, and the market uses continuous trading. A dedicated market-order type, IOC, batch auctions, dark pools, and ITCH/LOBSTER input adapters are not implemented.
- **Observations:** Public Python observations provide best quotes and remaining order quantities. They do not include depth-N book data or account state. Fill-notification integration is also not yet in place for the current continuous market and Python agents.
- **Logs:** Quotes are recorded at receipt, before the event is applied. `mid_series()` constructs a series from those quotes and returns that side's price when only one side exists. It does not always return the midpoint of two-sided quotes or a post-fill price.
- **Multiple markets:** Multiple markets can be registered in the kernel, but each book covers a single instrument, and the current log has no market ID. Integrated analysis across markets and multi-asset cash/settlement management require additional implementation.

## Repository structure and related documents

```text
include/lobcore/        Public headers for the order book, allocation rules, and logs
include/lobcore/kernel/ Events, markets, agents, random numbers, and delays
src/                   C++ implementation and reference order book
bindings/              pybind11 bindings
python/src/lobcore/     Agent, Experiment, log saving, and analysis helpers
tests/                 C++ specification, differential, and reproducibility tests
python/tests/          Python API, log, and random-number tests
bench/                 Benchmarks
docs/                  Design and research notes
```

- [Order book contract](include/lobcore/book.hpp) and [executable specification](tests/test_book.cpp)
- [Event log and replay](docs/stage2-event-log.md)
- [Discrete-event kernel design](docs/stage4-kernel.md)
- [Python interface](docs/stage5-python.md)
- [Counterfactual experiment infrastructure and division of responsibilities](docs/stage6-impact-experiment.md)
- [Correction notes for byte-level log reproducibility](docs/log-padding-investigation.md)
- [Literature and background material](docs/lobcore-context-pack.md)

Research models and individual experiments are maintained in [financial-abm-lab](https://github.com/yuitokyouni/financial-abm-lab). In the Stage 6 design, lobcore provides the infrastructure for comparison experiments, while YH012 handles agent models, impact evaluation, and visualization.
