# Methodology

## Overhead

For PARSEC:

```
overhead_pct = (traced_runtime - B0_runtime) / B0_runtime * 100
```

Runtime is wall time around the whole native process, including
input/output. It is not the PARSEC hooks ROI.

For CloudSuite Data Caching, the offered load is fixed and the
client is wrapped in `timeout --signal=TERM 30`, so wall time is
~30 s for every strategy. Runtime overhead is therefore
uninformative. The primary application metric is interval p99
latency (median of interval p99, not a pooled request p99).
Throughput is recorded but is not treated as the main overhead
signal. Exit 124 (timeout) is accepted.

## Calibration

Each candidate source is enabled alone on a dedicated ftrace
instance. Event counts come from the kernel `trace` snapshot after
tracing is turned off (the collector does not drain `trace_pipe`,
which would change drop accounting). Bytes and overrun come from
per-CPU buffer `stats`.

Unknown kernel fields stay null.

Source overhead is signed. A negative value is measurement noise,
not a clamped zero.

## Planner

Lexicographic: keep every `critical` source; if they alone exceed
the budget, the plan is infeasible and critical sources are not
dropped. `useful` then `bulk` are added cheapest-first using

```
cost = w_trace * norm(bytes/s) + w_overhead * norm(overhead)
```

Weights live in the budget YAML. Additive source overhead is an
estimator, not a claim that costs are independent.

## Baselines

- B0: no tracing
- B1: all candidates in one instance, same total `buffer_kb`
- B2: **same six candidates as B1**, static 70%/30% split - partitioning control
- B3: TraceArbiter admission plan from calibration
- REF: critical sources only, large buffer - not ground truth

## Primary metrics after holdout

Use, in this order:

1. Kernel `stats` overrun (overwrite loss in overwrite mode)
2. Which sources were admitted
3. Estimated admitted MB/s vs frozen cap (feasibility)
4. Application impact (runtime / p99)

Do not lead with snapshot `critical_retention_vs_ref`. REF is not
lossless on data-caching. A ratio above 1.0 is observer effect or
residual buffer contents, not extra accuracy.

`dropped events` staying 0 while overrun is large is expected when
ftrace overwrite is on.

## Retention

`critical_retention_vs_ref` is still computed as

```
count(sched_switch + sched_waking + sched_wakeup in snapshot)
/ same count under REF
```

It is a diagnostic, not a headline number.

## Infeasible plans

If critical sources alone exceed the MB/s or overhead cap, the
planner sets `feasible: false` and
`CRITICAL_SOURCES_EXCEED_BUDGET`, but it still enables every
critical source. Holdout executes that plan; it does not drop
critical sources to look feasible. `SKIPPED_OVER_BUDGET:<id>` on a
feasible plan means a useful/bulk source was omitted.

## Freeze / holdout

Calibration may be inspected. Budgets are frozen from observed
full-set cost, then holdout runs. Planner weights are not retuned
after holdout numbers are seen.
