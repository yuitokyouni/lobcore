from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import numpy as np

from lobcore._core import Kernel, KernelConfig
from lobcore.agent import Agent, BatchAdapter
from lobcore.log import LOG_DTYPE, ExperimentMeta


@dataclass
class ExperimentResult:
    log: np.ndarray
    meta: ExperimentMeta
    state_hash: int


def _kernel_log(kernel: Kernel) -> np.ndarray:
    raw = kernel.log_bytes()
    if not raw:
        return np.empty(0, dtype=LOG_DTYPE)
    return np.frombuffer(raw, dtype=LOG_DTYPE).copy()


class Experiment:
    def __init__(
        self,
        *,
        seed: int,
        agents: list[Agent] | None = None,
        end_time: int,
        rule: str = "price_time",
        n_markets: int = 1,
        strict: bool = False,
        agent_config: dict[str, Any] | None = None,
        step_fn: Any | None = None,
    ) -> None:
        self.seed = int(seed)
        self.end_time = int(end_time)
        self.rule = rule
        self.n_markets = int(n_markets)
        self.strict = strict
        self.agent_config = agent_config or {}
        self._agents = agents or []
        self._step_fn = step_fn

    def run(self) -> ExperimentResult:
        cfg = KernelConfig()
        cfg.end_time = self.end_time
        cfg.master_seed = self.seed
        kernel = Kernel(cfg)

        market_ids = [kernel.add_market(self.rule) for _ in range(self.n_markets)]

        adapter: BatchAdapter | None = None
        if self._step_fn is not None:
            step = self._step_fn
            n_agents = int(self.agent_config.get("n_agents", len(self._agents)))
        else:
            adapter = BatchAdapter(self._agents, strict=self.strict)
            step = adapter.step
            n_agents = len(self._agents)

        if n_agents == 0:
            raise ValueError("Experiment requires at least one agent")

        ids = kernel.add_batch_agents(step, n_agents)
        if adapter is not None:
            adapter.bind_kernel(kernel)
        for aid in ids:
            kernel.schedule_wakeup(1, aid)

        kernel.run()

        meta = ExperimentMeta(
            master_seed=self.seed,
            allocation_rule=self.rule,
            n_agents=n_agents,
            n_markets=len(market_ids),
            end_time=self.end_time,
            agent_config=self.agent_config,
        )
        state_hash = 0
        for mid in market_ids:
            state_hash ^= kernel.market_state_hash(mid)
        return ExperimentResult(log=_kernel_log(kernel), meta=meta, state_hash=state_hash)
