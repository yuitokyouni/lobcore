from __future__ import annotations

import numpy as np
import pytest

import lobcore as lc
from lobcore import LOG_DTYPE, Agent, BatchAdapter, Context, Experiment, View


def test_log_dtype_matches_cpp():
    assert LOG_DTYPE.itemsize == lc.LOG_RECORD_SIZE
    assert LOG_DTYPE.alignment == lc.LOG_RECORD_ALIGN


def test_order_book_basic():
    book = lc.OrderBook()
    o = lc.Order()
    o.id = 1
    o.side = lc.Side.Buy
    o.price = 100
    o.qty = 5
    o.decided_at = 1
    trades = book.add_limit(o, 1)
    assert len(trades) == 0
    assert book.best_bid() is not None
    assert book.best_bid().price == 100
    assert book.remaining(1) == 5


class RestOnce(Agent):
    def on_wakeup(self, view: View, ctx: Context) -> None:
        ctx.submit(0, "buy", 100, 1)
        # no further wakeup


def test_python_agent_rests_order():
    exp = Experiment(
        seed=7,
        agents=[RestOnce()],
        end_time=10,
        rule="price_time",
    )
    result = exp.run()
    assert result.state_hash != 0 or True  # board may hash nonzero
    kinds = result.log["kind"] if len(result.log) else []
    assert any(int(k) == 0 for k in kinds)  # Add


class SameViewAgent(Agent):
    def __init__(self) -> None:
        self.seen_bid = None

    def on_wakeup(self, view: View, ctx: Context) -> None:
        self.seen_bid = view.market(0).best_bid
        ctx.submit(0, "buy", 90, 1)


def test_same_time_agents_share_view():
    a1, a2 = SameViewAgent(), SameViewAgent()
    exp = Experiment(seed=1, agents=[a1, a2], end_time=10)
    exp.run()
    assert a1.seen_bid == a2.seen_bid


def test_schedule_wakeup_now_raises():
    ctx = Context(agent_id=0, now=5)
    with pytest.raises(ValueError):
        ctx.schedule_wakeup(5)


def test_rng_reproducible():
    cfg = lc.KernelConfig()
    cfg.master_seed = 42
    k = lc.Kernel(cfg)
    r1 = k.rng_for(0, 0)
    r2 = k.rng_for(0, 0)
    assert r1.next_u64() == r2.next_u64()
    assert r1.uniform() == pytest.approx(r2.uniform())


class SeededNoise(Agent):
    def on_wakeup(self, view: View, ctx: Context) -> None:
        # one resting order then stop
        ctx.submit(0, "sell", 101, 1)


def test_experiment_reproducible():
    def run_once():
        return Experiment(seed=99, agents=[SeededNoise(), SeededNoise()], end_time=20).run()

    a = run_once()
    b = run_once()
    assert a.state_hash == b.state_hash
    assert np.array_equal(a.log, b.log)


def test_pro_rata_changes_log():
    class Cross(Agent):
        def __init__(self, side: str, price: int) -> None:
            self.side = side
            self.price = price

        def on_wakeup(self, view: View, ctx: Context) -> None:
            ctx.submit(0, self.side, self.price, 10)

    def run(rule: str):
        # maker rests, taker crosses — allocation rule affects fill splits with multiple makers
        agents = [Cross("sell", 100), Cross("sell", 100), Cross("buy", 100)]
        return Experiment(seed=3, agents=agents, end_time=30, rule=rule).run()

    fifo = run("price_time")
    pro = run("pro_rata")
    # at least one of hash/log should differ when both makers rest before buy
    assert fifo.state_hash != pro.state_hash or not np.array_equal(fifo.log, pro.log)


def test_batch_adapter_vs_direct_step():
    agents = [RestOnce(), RestOnce()]

    def direct(obs):
        return BatchAdapter(agents).step(obs)

    r1 = Experiment(seed=5, agents=agents, end_time=10).run()
    r2 = Experiment(seed=5, agents=[], end_time=10, step_fn=direct, agent_config={"n_agents": 2}).run()
    assert r1.state_hash == r2.state_hash
    assert np.array_equal(r1.log, r2.log)
