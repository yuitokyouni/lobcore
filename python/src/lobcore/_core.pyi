# Auto-generated style stub for lobcore._core (Stage 5).
from __future__ import annotations

from typing import Any

LOG_RECORD_SIZE: int
LOG_RECORD_ALIGN: int

class Side:
    Buy: Side
    Sell: Side

class Level:
    price: int
    qty: int

class Order:
    id: int
    side: Side
    price: int
    qty: int
    decided_at: int

class Trade:
    maker_id: int
    taker_id: int
    price: int
    qty: int

class OrderBook:
    def add_limit(self, order: Order, received_at: int) -> list[Trade]: ...
    def cancel(self, order_id: int) -> bool: ...
    def best_bid(self) -> Level | None: ...
    def best_ask(self) -> Level | None: ...
    def remaining(self, order_id: int) -> int | None: ...
    def state_hash(self) -> int: ...

class Rng:
    def next_u64(self) -> int: ...
    def uniform(self) -> float: ...
    def normal(self, mu: float, sigma: float) -> float: ...
    def exponential(self, rate: float) -> float: ...
    def uniform_array(self, n: int) -> Any: ...

class MarketSnapshot:
    best_bid: Level | None
    best_ask: Level | None

class BatchObservation:
    now: int
    agent_ids: list[int]
    markets: list[MarketSnapshot]

class AddLimit:
    id: int
    side: Side
    price: int
    qty: int
    decided_at: int

class CancelOrder:
    id: int

class OrderSubmission:
    agent_id: int
    market_id: int
    msg: AddLimit | CancelOrder

class BatchAction:
    orders: list[OrderSubmission]
    next_wakeups: list[int]

class BatchRejectCounts:
    invalid_wakeup: int
    order_from_sleeping_agent: int

class KernelConfig:
    end_time: int
    master_seed: int

class Kernel:
    def __init__(self, config: KernelConfig = ...) -> None: ...
    def add_market(self, rule: str = ...) -> int: ...
    def add_batch_agents(self, step_fn: Any, n: int) -> list[int]: ...
    def schedule_wakeup(self, t: int, agent_id: int) -> None: ...
    def run(self) -> None: ...
    def now(self) -> int: ...
    def agent_count(self) -> int: ...
    def market_count(self) -> int: ...
    def master_seed(self) -> int: ...
    def end_time(self) -> int: ...
    def batch_rejects(self) -> BatchRejectCounts: ...
    def market_state_hash(self, market_id: int) -> int: ...
    def market_best_bid(self, market_id: int) -> Level | None: ...
    def market_best_ask(self, market_id: int) -> Level | None: ...
    def market_remaining(self, market_id: int, order_id: int) -> int | None: ...
    def log_bytes(self) -> bytes: ...
    def rng_for(self, agent: int, component: int) -> Rng: ...
