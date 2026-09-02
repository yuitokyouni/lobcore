from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from typing import Any

import numpy as np

from lobcore._core import LOG_RECORD_ALIGN, LOG_RECORD_SIZE

LOG_DTYPE = np.dtype(
    [
        ("seq", "u8"),
        ("kind", "u1"),
        ("side", "u1"),
        ("reason", "u1"),
        ("decided_at", "i8"),
        ("received_at", "i8"),
        ("order_id", "u8"),
        ("maker_id", "u8"),
        ("price", "i8"),
        ("qty", "i8"),
        ("best_bid_price", "i8"),
        ("best_bid_qty", "i8"),
        ("best_ask_price", "i8"),
        ("best_ask_qty", "i8"),
    ],
    align=True,
)

assert LOG_DTYPE.itemsize == LOG_RECORD_SIZE
assert LOG_DTYPE.alignment == LOG_RECORD_ALIGN


@dataclass
class ExperimentMeta:
    master_seed: int
    allocation_rule: str
    n_agents: int
    n_markets: int
    end_time: int
    lobcore_version: str = "0.1.0"
    agent_config: dict[str, Any] = field(default_factory=dict)


def write_log_file(path: str, meta: ExperimentMeta, log: np.ndarray) -> None:
    header = json.dumps(asdict(meta), separators=(",", ":")).encode("utf-8")
    with open(path, "wb") as f:
        f.write(len(header).to_bytes(8, "little"))
        f.write(header)
        f.write(np.ascontiguousarray(log, dtype=LOG_DTYPE).tobytes())


def read_log_file(path: str) -> tuple[ExperimentMeta, np.ndarray]:
    with open(path, "rb") as f:
        header_len = int.from_bytes(f.read(8), "little")
        header = json.loads(f.read(header_len).decode("utf-8"))
        payload = f.read()
    meta = ExperimentMeta(**header)
    log = np.frombuffer(payload, dtype=LOG_DTYPE).copy()
    return meta, log
