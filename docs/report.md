# TraceArbiter technical report

See [README.md](../README.md) for the results-first summary.

## Identity after holdout

TraceArbiter is workload-calibrated trace admission under a finite
ftrace buffer. Full tracing is not made cheaper; the tool decides
when full tracing is not affordable.

The first design had two axes: source admission, and multi-instance
buffer allocation. Holdout `native-holdout-v1` (165/165 OK) supported
the first. It did not support the second as a consistent win. B1
(all sources, one buffer) vs B2 (all sources, static 70/30 split)
holds the source set fixed. That control never produces the 9x-121x
overrun drop. The drop appears only when B3 excludes
`raw_syscalls:sys_enter`. Put that source back and the gap closes;
blackscholes large B3 is worse than B1 (1,868,815 vs 1,004,742).

This is not a multi-buffer optimizer.

## Binding constraint on this host

```text
CPU overhead          not binding  (full-set +0.001% ... +0.39%)
ring-buffer capacity  binding      (overrun 10^4 ... 10^8)
```

`overrun` is overwrite loss. `dropped events` stayed 0.

Primary metrics: overrun, admitted source set, budget feasibility,
application impact. Snapshot count / REF is not an oracle.
data-caching REF still overran 2,485,957 at 65536 KB/CPU.

## Fail-explicit planner

data-caching small: estimated 2.598 MB/s vs cap 1.628 MB/s ->
`CRITICAL_SOURCES_EXCEED_BUDGET`. Critical three remain enabled.
Overrun is still 9,329,201. Infeasible was not disguised, and
"critical admitted" is not "critical preserved."

## Provenance

The campaign ran from a local snapshot. Published commits are the
public source layout and are not the holdout execution SHA.

## Resume bullet

> **TraceArbiter - workload-calibrated ftrace admission**
> C++20 controller on Linux ftrace/tracefs. It measures each source's
> real rate, then admits critical, useful, then bulk under a finite
> per-CPU ring-buffer / MB/s budget. CloudSuite Data Caching plus two
> PARSEC native workloads, 5-rep holdout, 165/165. A static split of
> the same six sources (B2) did not beat one buffer (B1) on overrun.
> Overrun fell 9.1x-121x only in cells that dropped bulk `sys_enter`.
> On this host the binding constraint was ring-buffer capacity, not
> CPU overhead.

## Campaign

- Host: Intel Core i9-14900K, Linux 7.0.0-28-generic, GCC 13.3.0
- Calibration `calibration-v1` (135 OK) then freeze then
  `native-holdout-v1`
- Pin: `taskset -c 2,4,6,8` for PARSEC
- No v2. No planner retune after holdout.
