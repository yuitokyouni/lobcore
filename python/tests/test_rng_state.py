"""A kernel owns mutable streams; looking one up must never restart it."""

import gc

import pytest

import lobcore as lc


def make_kernel(registered=False):
    config = lc.KernelConfig()
    config.master_seed = 14
    kernel = lc.Kernel(config)
    if registered:
        kernel.add_batch_agents(lambda observation: lc.BatchAction(), 100)
    return kernel


@pytest.mark.parametrize("registered", [False, True])
@pytest.mark.parametrize("component", [0, 1, 2, 3])
def test_reacquiring_agent_stream_continues_sequence(registered, component):
    actual_kernel = make_kernel(registered)
    expected_kernel = make_kernel(registered)
    expected_rng = expected_kernel.rng_for(55, component)
    expected = [expected_rng.next_u64() for _ in range(8)]
    actual = [actual_kernel.rng_for(55, component).next_u64() for _ in range(8)]
    assert actual == expected


def test_reacquiring_sentinel_stream_continues_sequence():
    actual_kernel, expected_kernel = make_kernel(), make_kernel()
    expected_rng = expected_kernel.sentinel_rng(0)
    expected = [expected_rng.normal(0.0, 0.25) for _ in range(8)]
    actual = [actual_kernel.sentinel_rng(0).normal(0.0, 0.25) for _ in range(8)]
    assert actual == expected


def test_context_stream_survives_new_contexts_and_other_draws():
    actual_kernel, expected_kernel = make_kernel(True), make_kernel(True)
    expected_rng = expected_kernel.rng_for(55, 0)
    expected = [expected_rng.exponential(1 / 800) for _ in range(8)]
    actual = []
    for now in range(1, 9):
        context = lc.Context(agent_id=55, now=now, kernel=actual_kernel)
        actual.append(context.rng(0).exponential(1 / 800))
        context.rng(1).uniform()
        actual_kernel.rng_for(56, 0).uniform()
        actual_kernel.sentinel_rng(0).normal(0.0, 1.0)
    assert actual == expected


@pytest.mark.parametrize("sentinel", [False, True])
def test_stream_keeps_owning_kernel_alive(sentinel):
    kernel, expected_kernel = make_kernel(), make_kernel()
    rng = kernel.sentinel_rng(0) if sentinel else kernel.rng_for(55, 0)
    expected_rng = (
        expected_kernel.sentinel_rng(0) if sentinel else expected_kernel.rng_for(55, 0)
    )
    del kernel
    gc.collect()
    assert [rng.next_u64() for _ in range(8)] == [expected_rng.next_u64() for _ in range(8)]
