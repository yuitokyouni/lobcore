# Python RNG state correction — 2026-09-17

The Python bindings through `6aff004bf77cc7c70aceea6e409f8155f7cd94ce`
returned copies of kernel RNG streams. The lambda wrapping `rng_for` deduced a
value return type despite `Kernel::rng_for` returning `Rng&`; `sentinel_rng`
also lacked an explicit reference policy.

Consequently, repeatedly calling `ctx.rng(component)` restarted that stream
from the unchanged kernel state. A Python agent could receive the same wakeup
interval, quantity, side, or price draw at every decision. Retaining one returned
RNG object and drawing repeatedly from it advanced only that copy.

Both bindings now return the kernel-owned stream with `reference_internal`.
The kernel remains alive for the lifetime of an exported stream. C++ RNG
algorithms, stream keys, seed mixing, and the Python call signatures are unchanged.

## Required behavior

- Reacquiring one stream continues its sequence, including across new Contexts.
- Draws from other agents/components do not consume that stream.
- Separate kernels with equal seeds reproduce equal sequences.
- A held stream remains usable when the caller releases its kernel variable.

The earlier Python reproducibility tests incorrectly compared two stream lookups
from the same kernel as if they were independent copies. They now compare
separate kernels. New regressions cover registered and detached agent streams,
sentinel streams, Context recreation/interleaving, and owner lifetime. Ten
state-continuation cases fail against the former binding.

## Existing experiment outputs

Deterministic replay and pre-intervention F/B equality do not certify correct
sampling: two runs can repeat the same RNG bug. Python-agent results produced
with the affected access pattern require rerunning. In particular, YH012's
archived seed-42 and eligible-39 experiments in financial-abm-lab are retained
as historical outputs and must not be interpreted as corrected market responses.

The correction restores the intended mutable-stream contract. It changes
Python simulation trajectories; previous seed eligibility and effect estimates
must be checked again. Native C++ agents directly accessing kernel streams were
not subject to the Python copy boundary.

## Validation

- Python suite: 43 tests passed, including the 12 new regression cases.
- C++ Debug suite with ASan/UBSan: 79 tests passed. Leak detection was disabled
  only for this local run because LeakSanitizer cannot operate under the hosted
  environment's ptrace; address and undefined-behavior checks remained enabled.
- financial-abm-lab YH012 suite: 37 tests passed with the corrected extension.
