from __future__ import annotations

import numpy as np

from lobcore.log import LOG_DTYPE

EventKindAdd = 0
EventKindFill = 1
EventKindCancel = 2


def order_id_range(agent_id: int) -> tuple[int, int]:
    """Context の自動採番 `(agent_id << 32) + seq` と同じ帯域。"""
    base = int(agent_id) << 32
    return base, base + (1 << 32) - 1


def order_id_belongs_to_agent(order_id: int, agent_id: int) -> bool:
    lo, hi = order_id_range(agent_id)
    oid = int(order_id)
    return lo <= oid <= hi


def record_from_agent(rec: np.void, agent_id: int) -> bool:
    oid = int(rec["order_id"])
    mid = int(rec["maker_id"])
    return order_id_belongs_to_agent(oid, agent_id) or order_id_belongs_to_agent(mid, agent_id)


def record_from_order_ids(rec: np.void, order_ids: set[int]) -> bool:
    oid = int(rec["order_id"])
    mid = int(rec["maker_id"])
    return oid in order_ids or mid in order_ids


def filter_log_exclude_agent(log: np.ndarray, agent_id: int) -> np.ndarray:
    if len(log) == 0:
        return log
    mask = np.array([not record_from_agent(rec, agent_id) for rec in log], dtype=bool)
    return log[mask]


def filter_log_exclude_order_ids(log: np.ndarray, order_ids: set[int]) -> np.ndarray:
    if len(log) == 0:
        return log
    mask = np.array([not record_from_order_ids(rec, order_ids) for rec in log], dtype=bool)
    return log[mask]


def log_before_time(log: np.ndarray, before: int) -> np.ndarray:
    if len(log) == 0:
        return log
    return log[log["received_at"] < int(before)]


def logs_byte_equal(a: np.ndarray, b: np.ndarray) -> bool:
    if len(a) != len(b):
        return False
    if len(a) == 0:
        return True
    return np.array_equal(a, b)


def mid_from_record(rec: np.void) -> float | None:
    bid_p = int(rec["best_bid_price"])
    ask_p = int(rec["best_ask_price"])
    bid_q = int(rec["best_bid_qty"])
    ask_q = int(rec["best_ask_qty"])
    if bid_q > 0 and ask_q > 0:
        return (bid_p + ask_p) / 2.0
    if bid_q > 0:
        return float(bid_p)
    if ask_q > 0:
        return float(ask_p)
    return None


def mid_series(log: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(received_at[], mid[]) — mid が None の行は省略。"""
    times: list[int] = []
    mids: list[float] = []
    for rec in log:
        mid = mid_from_record(rec)
        if mid is None:
            continue
        times.append(int(rec["received_at"]))
        mids.append(mid)
    return np.asarray(times, dtype=np.int64), np.asarray(mids, dtype=np.float64)


def impact_delta(
    factual: np.ndarray,
    baseline: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    """同一 received_at の mid 差分。F/B で時刻集合が一致するとき用。"""
    f_t, f_m = mid_series(factual)
    b_t, b_m = mid_series(baseline)
    common = np.intersect1d(f_t, b_t)
    if common.size == 0:
        return common, np.empty(0, dtype=np.float64)
    f_idx = {int(t): float(m) for t, m in zip(f_t, f_m, strict=True)}
    b_idx = {int(t): float(m) for t, m in zip(b_t, b_m, strict=True)}
    delta = np.array([f_idx[int(t)] - b_idx[int(t)] for t in common], dtype=np.float64)
    return common, delta
