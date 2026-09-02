from __future__ import annotations

from lobcore import (
    Agent,
    Context,
    Experiment,
    View,
    filter_log_exclude_order_ids,
    log_before_time,
    logs_byte_equal,
    mid_series,
    order_id_range,
)


class WorldSell(Agent):
    def __init__(self) -> None:
        self.rng_samples: list[int] = []

    def on_wakeup(self, view: View, ctx: Context) -> None:
        self.rng_samples.append(int(ctx.rng(0).next_u64()))
        if view.now == 2:
            ctx.submit(0, "sell", 100, 5)
        if view.now < 10:
            ctx.schedule_wakeup(view.now + 1)


class ExperimentalBuy(Agent):
    def __init__(self) -> None:
        self.rng_samples: list[int] = []

    def on_wakeup(self, view: View, ctx: Context) -> None:
        self.rng_samples.append(int(ctx.rng(0).next_u64()))
        if view.now == 5:
            ctx.submit(0, "buy", 50, 3)
        if view.now < 10:
            ctx.schedule_wakeup(view.now + 1)


class WorldObserve(Agent):
    def __init__(self) -> None:
        self.rng_samples: list[int] = []

    def on_wakeup(self, view: View, ctx: Context) -> None:
        self.rng_samples.append(int(ctx.rng(0).next_u64()))
        if view.now < 10:
            ctx.schedule_wakeup(view.now + 1)


def _fresh_agents() -> tuple[WorldSell, ExperimentalBuy, WorldObserve]:
    return WorldSell(), ExperimentalBuy(), WorldObserve()


def test_run_pair_log_prefix_and_filter():
    agents = _fresh_agents()
    pair = Experiment(seed=4242, agents=list(agents), end_time=10).run_pair(suppress_agent_ids=[1])

    lo, _ = order_id_range(1)
    exp_order_id = lo + 1

    prefix_f = log_before_time(pair.factual.log, 5)
    prefix_b = log_before_time(pair.baseline.log, 5)
    assert logs_byte_equal(prefix_f, prefix_b)

    filtered = filter_log_exclude_order_ids(pair.factual.log, {exp_order_id})
    assert logs_byte_equal(filtered, pair.baseline.log)


def test_suppress_preserves_all_agent_rng_streams():
    f0, f1, f2 = _fresh_agents()
    Experiment(seed=4242, agents=[f0, f1, f2], end_time=10).run()

    b0, b1, b2 = _fresh_agents()
    Experiment(seed=4242, agents=[b0, b1, b2], end_time=10).run(suppress_agent_ids=[1])

    assert f0.rng_samples == b0.rng_samples
    assert f1.rng_samples == b1.rng_samples
    assert f2.rng_samples == b2.rng_samples


def test_mid_series_from_log():
    agents = _fresh_agents()
    pair = Experiment(seed=4242, agents=list(agents), end_time=10).run_pair(suppress_agent_ids=[1])
    times, mids = mid_series(pair.factual.log)
    assert len(times) == len(mids)
    assert len(times) > 0
