from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from lobcore._core import (
    AddLimit,
    BatchAction,
    BatchObservation,
    CancelOrder,
    OrderSubmission,
    Side,
)


@dataclass
class LevelView:
    price: int
    qty: int


@dataclass
class MarketView:
    best_bid: LevelView | None
    best_ask: LevelView | None
    _remaining_fn: Any = field(default=None, repr=False)

    def remaining(self, order_id: int) -> int | None:
        if self._remaining_fn is None:
            return None
        return self._remaining_fn(order_id)


@dataclass
class View:
    now: int
    markets: list[MarketView]

    @classmethod
    def from_observation(cls, obs: BatchObservation, remaining_fns: list[Any] | None = None) -> View:
        markets: list[MarketView] = []
        for i, snap in enumerate(obs.markets):
            bid = None if snap.best_bid is None else LevelView(snap.best_bid.price, snap.best_bid.qty)
            ask = None if snap.best_ask is None else LevelView(snap.best_ask.price, snap.best_ask.qty)
            rem = None
            if remaining_fns is not None and i < len(remaining_fns):
                rem = remaining_fns[i]
            markets.append(MarketView(best_bid=bid, best_ask=ask, _remaining_fn=rem))
        return cls(now=int(obs.now), markets=markets)

    def market(self, market_id: int) -> MarketView:
        return self.markets[market_id]


class Context:
    def __init__(self, agent_id: int, now: int, order_id_base: int | None = None) -> None:
        self.agent_id = int(agent_id)
        self.now = int(now)
        self.orders: list[OrderSubmission] = []
        self.next_wakeup: int | None = None
        self._seq = 0
        # エージェント ID 上位 32bit + 連番で衝突を避ける
        self._order_id_base = (
            order_id_base if order_id_base is not None else (self.agent_id << 32)
        )

    def submit(
        self,
        market_id: int,
        side: str,
        price: int,
        qty: int,
        order_id: int | None = None,
    ) -> int:
        if qty <= 0:
            raise ValueError("qty must be positive")
        if order_id is None:
            self._seq += 1
            order_id = self._order_id_base + self._seq
        side_enum = Side.Buy if side.lower() in ("buy", "b", "bid") else Side.Sell
        add = AddLimit()
        add.id = int(order_id)
        add.side = side_enum
        add.price = int(price)
        add.qty = int(qty)
        sub = OrderSubmission()
        sub.agent_id = self.agent_id
        sub.market_id = int(market_id)
        sub.msg = add
        self.orders.append(sub)
        return int(order_id)

    def cancel(self, market_id: int, order_id: int) -> None:
        c = CancelOrder()
        c.id = int(order_id)
        sub = OrderSubmission()
        sub.agent_id = self.agent_id
        sub.market_id = int(market_id)
        sub.msg = c
        self.orders.append(sub)

    def schedule_wakeup(self, t: int) -> None:
        if t <= self.now:
            raise ValueError("wakeup time must be > now")
        self.next_wakeup = int(t)


class Agent:
    def on_wakeup(self, view: View, ctx: Context) -> None:
        raise NotImplementedError

    def on_notification(self, note: Any, view: View, ctx: Context) -> None:
        return None


class BatchAdapter:
    def __init__(self, agents: list[Agent], *, strict: bool = False) -> None:
        self._agents = agents
        self._strict = strict
        self._stopped: set[int] = set()

    def step(self, obs: BatchObservation) -> BatchAction:
        view = View.from_observation(obs)
        orders: list[OrderSubmission] = []
        wakeups: list[int] = []
        for aid in obs.agent_ids:
            aid_i = int(aid)
            if aid_i in self._stopped or aid_i >= len(self._agents):
                wakeups.append(0)
                continue
            ctx = Context(agent_id=aid_i, now=int(obs.now))
            try:
                self._agents[aid_i].on_wakeup(view, ctx)
            except Exception:
                if self._strict:
                    raise
                self._stopped.add(aid_i)
                wakeups.append(0)
                continue
            orders.extend(ctx.orders)
            wakeups.append(ctx.next_wakeup or 0)
        act = BatchAction()
        act.orders = orders
        act.next_wakeups = wakeups
        return act
