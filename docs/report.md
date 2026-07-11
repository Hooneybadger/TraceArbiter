# TraceArbiter technical report

See `README.md` for the results-first summary.

## Identity after holdout

TraceArbiter is **workload-calibrated trace admission under finite
ftrace buffer capacity**. It does not make full tracing cheaper. It
decides when full tracing is not affordable.

The first design had two axes:

1. Source admission
2. Multi-instance buffer allocation

Holdout `native-holdout-v1` (165/165 OK) supported (1). It did not
support (2) as a consistent improvement. B1 (all sources, one
buffer) vs B2 (all sources, static 70/30 split) is the control:
when the source set is fixed, partitioning does not produce the
9×–121× overrun drop. That drop appears only when B3 excludes
`raw_syscalls:sys_enter`. Putting that source back closes the gap;
blackscholes large B3 is worse than B1 (1,868,815 vs 1,004,742).

Do not describe the project as a multi-buffer optimizer.

## Binding constraint on this host

```text
CPU overhead          not binding  (full-set +0.001% … +0.39%)
ring-buffer capacity  binding      (overrun 10^4 … 10^8)
```

`overrun` is overwrite loss. `dropped events` stayed 0.

Primary metrics: overrun, admitted source set, budget feasibility,
application impact. Snapshot count / REF is not an oracle.
data-caching REF still overran 2,485,957 at 65536 KB/CPU.

## Fail-explicit planner

data-caching small: estimated 2.598 MB/s vs cap 1.628 MB/s →
`CRITICAL_SOURCES_EXCEED_BUDGET`. Critical three remain enabled.
Overrun is still 9,329,201. Infeasible was not disguised, and
"critical admitted" is not "critical preserved."

## Provenance

The campaign ran from a local snapshot. Published commits are the
public source layout and are not the holdout execution SHA.

## Resume bullet

> **TraceArbiter — Workload-calibrated ftrace admission**
> C++20과 Linux ftrace/tracefs로 source별 실제 trace rate를
> calibration하고, 유한 per-CPU ring buffer / MB/s budget에서
> critical→useful→bulk 순으로 source를 admission하는 컨트롤러를
> 구현했습니다. CloudSuite Data Caching과 PARSEC native 2종
> 5-rep holdout(165/165)에서, 동일 source set의 static split(B2)은
> 단일 버퍼(B1) 대비 일관된 overrun 이득이 없었고, budget 때문에
> bulk `sys_enter`를 제외한 셀에서만 overrun이 9.1×~121× 감소했습니다.
> 이 호스트의 binding constraint는 CPU overhead가 아니라 ring-buffer
> capacity였습니다.

## Campaign

- Host: Intel Core i9-14900K, Linux 7.0.0-28-generic, GCC 13.3.0
- Calibration `calibration-v1` (135 OK) then freeze then
  `native-holdout-v1`
- Pin: `taskset -c 2,4,6,8` for PARSEC
- No v2. No planner retune after holdout.
