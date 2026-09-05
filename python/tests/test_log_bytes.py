"""Raw-record invariants, including the five bytes outside NumPy fields."""

from dataclasses import asdict
import json

import numpy as np
import pytest

from lobcore import (
    Agent,
    Experiment,
    ExperimentMeta,
    Kernel,
    KernelConfig,
    BatchAdapter,
    LOG_DTYPE,
    read_log_file,
    write_log_file,
    logs_byte_equal,
    log_before_time,
    filter_log_exclude_agent,
    filter_log_exclude_order_ids,
)
from lobcore.experiment import _kernel_log


def patterned_log():
    raw = bytearray(96 * 6)
    log = np.frombuffer(raw, dtype=LOG_DTYPE)
    log["received_at"] = [1, 4, 2, 5, 3, 6]
    log["order_id"] = [(i << 32) + 1 for i in range(6)]
    for i in range(6):
        raw[i * 96 + 11 : i * 96 + 16] = bytes([0xA0 + i]) * 5
    return log


def test_kernel_log_capture_preserves_native_padding():
    raw = patterned_log().tobytes()

    class FakeKernel:
        def log_bytes(self):
            return raw

    actual = _kernel_log(FakeKernel())
    assert actual.tobytes() == raw
    assert actual.flags.writeable  # preserve the existing mutable Python result


def test_reader_preserves_every_saved_byte(tmp_path):
    expected = patterned_log()
    meta = ExperimentMeta(13, "price_time", 6, 1, 10)
    header = json.dumps(asdict(meta)).encode()
    path = tmp_path / "raw.bin"
    path.write_bytes(len(header).to_bytes(8, "little") + header + expected.tobytes())
    actual_meta, actual = read_log_file(path)
    assert actual_meta == meta
    assert actual.tobytes() == expected.tobytes()
    assert actual.flags.writeable


@pytest.mark.parametrize(
    "selection",
    [slice(None), slice(None, None, 2), slice(None, None, -1), slice(0, 0)],
    ids=["contiguous", "strided", "reversed", "empty"],
)
def test_log_file_roundtrip_preserves_every_record_byte(tmp_path, selection):
    expected = patterned_log()[selection]
    original = expected.tobytes()
    assert LOG_DTYPE.itemsize == 96
    assert len(original) == 96 * len(expected)
    meta = ExperimentMeta(13, "price_time", 6, 1, 10)
    path = tmp_path / "strided.bin"
    write_log_file(path, meta, expected)
    raw = path.read_bytes()
    offset = 8 + int.from_bytes(raw[:8], "little")
    assert raw[offset:] == original
    restored_meta, restored = read_log_file(path)
    assert restored_meta == meta
    assert restored.dtype == expected.dtype
    assert restored.shape == expected.shape
    # Independent of logs_byte_equal: compare every byte, including deliberately
    # nonzero padding, so paired writer/reader normalization cannot hide a bug.
    assert restored.tobytes() == original
    assert logs_byte_equal(expected, restored)
    assert expected.tobytes() == original


def test_time_filter_preserves_bytes_and_supports_unordered_input():
    log = patterned_log()
    expected = b"".join(log[i : i + 1].tobytes() for i in [0, 2, 4])
    assert log_before_time(log, 4).tobytes() == expected


def test_order_and_agent_filters_preserve_surviving_bytes():
    log = patterned_log()
    expected = b"".join(log[i : i + 1].tobytes() for i in [0, 1, 3, 4, 5])
    assert filter_log_exclude_order_ids(log, {(2 << 32) + 1}).tobytes() == expected
    assert filter_log_exclude_agent(log, 2).tobytes() == expected


def test_byte_comparison_detects_padding_only_difference():
    first = patterned_log()
    raw = bytearray(first.tobytes())
    raw[11] ^= 1
    second = np.frombuffer(raw, dtype=LOG_DTYPE)
    assert np.array_equal(first, second)
    assert not logs_byte_equal(first, second)
    assert logs_byte_equal(first[::2], first[::2])


class MarketOrders(Agent):
    def on_wakeup(self, view, ctx):
        ctx.submit(0, "sell", 100, 4)
        ctx.submit(0, "buy", 100, 2)
        if view.now < 100:
            ctx.schedule_wakeup(view.now + 3)


class Observer(Agent):
    def __init__(self, interval):
        self.interval = interval

    def on_wakeup(self, view, ctx):
        _ = view.market(0).best_ask
        if view.now + self.interval <= 100:
            ctx.schedule_wakeup(view.now + self.interval)


def native_run(interval=None):
    config = KernelConfig()
    config.master_seed = 13
    config.end_time = 100
    kernel = Kernel(config)
    kernel.add_market()
    agents = [MarketOrders()]
    if interval is not None:
        agents.append(Observer(interval))
    adapter = BatchAdapter(agents, strict=True)
    ids = kernel.add_batch_agents(adapter.step, len(agents))
    adapter.bind_kernel(kernel)
    for aid in ids:
        kernel.schedule_wakeup(1, aid)
    kernel.run()
    return kernel.log_bytes(), kernel.market_state_hash(0)


def test_native_bytes_and_state_do_not_depend_on_passive_observer_frequency():
    samples = [native_run(), native_run(), native_run(1), native_run(25)]
    assert all(sample == samples[0] for sample in samples)
    records = np.frombuffer(samples[0][0], dtype=np.uint8).reshape(-1, 96)
    assert not records[:, 11:16].any()


def test_python_experiment_and_disk_roundtrip_are_byte_exact(tmp_path):
    result = Experiment(seed=13, agents=[MarketOrders()], end_time=100).run()
    records = np.frombuffer(result.log.tobytes(), dtype=np.uint8).reshape(-1, 96)
    assert not records[:, 11:16].any()
    path = tmp_path / "experiment.bin"
    write_log_file(path, result.meta, result.log)
    _, restored = read_log_file(path)
    assert restored.tobytes() == result.log.tobytes()
    assert logs_byte_equal(result.log, restored)
