# Results

Numbers from `artifacts/analysis/report.json`
(`native-holdout-v1`, 165 validated OK rows, median of 5).

Primary columns: **overrun**, **admitted sources**, **feasible**.
Snapshot retention vs REF is recorded but is not an accuracy claim.

Host: Intel Core i9-14900K, kernel 7.0.0-28-generic. No campaign
git SHA; checksums in `artifacts/provenance/`.

Enabled is `critical/useful/bulk`. B1 and B2 are always `3/2/1`.

## Control: B1 vs B2 (same six sources)

This table is the partitioning control. B2 is not omitted.

| Workload | Budget | B1 overrun | B2 overrun | B3 overrun | B3 admits bulk? |
|---|---|---:|---:|---:|---|
| blackscholes | small | 2,505,416 | 2,662,293 | 331,374 | no |
| blackscholes | medium | 2,389,941 | 2,199,687 | 126,839 | no |
| blackscholes | large | 1,004,742 | 1,664,378 | 1,868,815 | yes |
| swaptions | small | 2,832,425 | 2,866,069 | 23,408 | no |
| swaptions | medium | 2,730,879 | 2,799,180 | 2,746,747 | yes |
| swaptions | large | 1,843,095 | 2,005,440 | 2,538,237 | yes |
| data-caching | small | 84,429,583 | 84,892,596 | 9,329,201 | no (infeasible) |
| data-caching | medium | 84,232,579 | 84,038,564 | 9,169,708 | no |
| data-caching | large | 81,363,762 | 80,234,327 | 78,720,800 | yes |

B2 never produces the 9x-121x overrun drop. That drop appears only
when B3 excludes `raw_syscalls:sys_enter`.

## Frozen budgets

| Budget | buffer_kb | max_target_overhead_pct | max_trace_mb_per_sec |
|---|---:|---:|---:|
| small | 256 | 0.250000 | 1.628403 |
| medium | 2048 | 0.375000 | 3.256805 |
| large | 16384 | 0.875000 | 6.513610 |

## B3 feasibility (planner estimate vs cap)

| Workload | Budget | Estimated MB/s | Cap MB/s | Feasible | Enabled |
|---|---|---:|---:|---|---|
| blackscholes | small | 1.107 | 1.628 | yes | 3/2/0 |
| blackscholes | medium | 1.107 | 3.257 | yes | 3/2/0 |
| blackscholes | large | 3.947 | 6.514 | yes | 3/2/1 |
| swaptions | small | 0.796 | 1.628 | yes | 3/2/0 |
| swaptions | medium | 3.085 | 3.257 | yes | 3/2/1 |
| swaptions | large | 3.085 | 6.514 | yes | 3/2/1 |
| data-caching | small | 2.598 | 1.628 | **no** | 3/0/0 |
| data-caching | medium | 3.071 | 3.257 | yes | 3/2/0 |
| data-caching | large | 5.661 | 6.514 | yes | 3/2/1 |

data-caching small reason: `CRITICAL_SOURCES_EXCEED_BUDGET`.
Critical sources remain enabled. Overrun 9,329,201.

## Holdout medians (n=5), full grid

| Workload | Strategy | Budget | Runtime s | Overhead % | Overrun | p99 ms | Feasible | c/u/b |
|---|---|---|---:|---:|---:|---:|---|---|
| blackscholes | B0 | none | 12.946 | - | 0 | - | yes | 0/0/0 |
| blackscholes | B1 | small | 13.011 | 0.500 | 2,505,416 | - | yes | 3/2/1 |
| blackscholes | B2 | small | 13.115 | 1.302 | 2,662,293 | - | yes | 3/2/1 |
| blackscholes | B3 | small | 13.083 | 1.057 | 331,374 | - | yes | 3/2/0 |
| blackscholes | B1 | medium | 13.153 | 1.600 | 2,389,941 | - | yes | 3/2/1 |
| blackscholes | B2 | medium | 12.992 | 0.353 | 2,199,687 | - | yes | 3/2/1 |
| blackscholes | B3 | medium | 12.999 | 0.411 | 126,839 | - | yes | 3/2/0 |
| blackscholes | B1 | large | 13.266 | 2.474 | 1,004,742 | - | yes | 3/2/1 |
| blackscholes | B2 | large | 12.999 | 0.413 | 1,664,378 | - | yes | 3/2/1 |
| blackscholes | B3 | large | 12.948 | 0.012 | 1,868,815 | - | yes | 3/2/1 |
| blackscholes | REF | ref | 12.922 | -0.185 | 0 | - | yes | 3/0/0 |
| swaptions | B0 | none | 15.531 | - | 0 | - | yes | 0/0/0 |
| swaptions | B1 | small | 15.575 | 0.282 | 2,832,425 | - | yes | 3/2/1 |
| swaptions | B2 | small | 15.595 | 0.407 | 2,866,069 | - | yes | 3/2/1 |
| swaptions | B3 | small | 15.672 | 0.906 | 23,408 | - | yes | 3/2/0 |
| swaptions | B1 | medium | 15.600 | 0.438 | 2,730,879 | - | yes | 3/2/1 |
| swaptions | B2 | medium | 15.616 | 0.546 | 2,799,180 | - | yes | 3/2/1 |
| swaptions | B3 | medium | 15.624 | 0.599 | 2,746,747 | - | yes | 3/2/1 |
| swaptions | B1 | large | 15.582 | 0.323 | 1,843,095 | - | yes | 3/2/1 |
| swaptions | B2 | large | 15.618 | 0.555 | 2,005,440 | - | yes | 3/2/1 |
| swaptions | B3 | large | 15.613 | 0.527 | 2,538,237 | - | yes | 3/2/1 |
| swaptions | REF | ref | 15.433 | -0.636 | 0 | - | yes | 3/0/0 |
| data-caching | B0 | none | 30.045 | - | 0 | 0.0221 | yes | 0/0/0 |
| data-caching | B1 | small | 30.092 | 0.155 | 84,429,583 | 0.0231 | yes | 3/2/1 |
| data-caching | B2 | small | 30.081 | 0.120 | 84,892,596 | 0.0221 | yes | 3/2/1 |
| data-caching | B3 | small | 30.095 | 0.167 | 9,329,201 | 0.0221 | no | 3/0/0 |
| data-caching | B1 | medium | 30.087 | 0.141 | 84,232,579 | 0.0231 | yes | 3/2/1 |
| data-caching | B2 | medium | 30.078 | 0.110 | 84,038,564 | 0.0231 | yes | 3/2/1 |
| data-caching | B3 | medium | 30.081 | 0.120 | 9,169,708 | 0.0221 | yes | 3/2/0 |
| data-caching | B1 | large | 30.057 | 0.040 | 81,363,762 | 0.0231 | yes | 3/2/1 |
| data-caching | B2 | large | 30.054 | 0.031 | 80,234,327 | 0.0221 | yes | 3/2/1 |
| data-caching | B3 | large | 30.062 | 0.055 | 78,720,800 | 0.0221 | yes | 3/2/1 |
| data-caching | REF | ref | 30.048 | 0.010 | 2,485,957 | 0.0221 | yes | 3/0/0 |

## Calibration per-source

| Workload | Source | Overhead % vs B0 | Bytes/s |
|---|---|---:|---:|
| blackscholes | `sched:sched_switch` | -0.1377 | 249,466 |
| blackscholes | `sched:sched_waking` | -0.5058 | 75,913 |
| blackscholes | `sched:sched_wakeup` | -0.2015 | 74,007 |
| blackscholes | `sched:sched_process_exit` | -0.3330 | 274 |
| blackscholes | `exceptions:page_fault_user` | +0.8649 | 761,020 |
| blackscholes | `raw_syscalls:sys_enter` | +0.0300 | 2,977,784 |
| blackscholes | `_full` | +0.2404 | 3,415,008 |
| swaptions | `sched:sched_switch` | -0.5661 | 235,863 |
| swaptions | `sched:sched_waking` | -0.7062 | 72,637 |
| swaptions | `sched:sched_wakeup` | -0.2795 | 68,789 |
| swaptions | `sched:sched_process_exit` | -0.7059 | 278 |
| swaptions | `exceptions:page_fault_user` | -0.5292 | 457,373 |
| swaptions | `raw_syscalls:sys_enter` | -0.4851 | 2,399,888 |
| swaptions | `_full` | +0.3915 | 2,052,286 |
| data-caching | `sched:sched_switch` | +0.0025 | 829,677 |
| data-caching | `sched:sched_waking` | -0.0032 | 1,264,011 |
| data-caching | `sched:sched_wakeup` | +0.0042 | 630,835 |
| data-caching | `sched:sched_process_exit` | +0.0045 | 327 |
| data-caching | `exceptions:page_fault_user` | +0.0021 | 495,110 |
| data-caching | `raw_syscalls:sys_enter` | -0.0003 | 2,715,666 |
| data-caching | `_full` | +0.0011 | 3,016,342 |

## Figures

1. [figure1_architecture.png](figures/figure1_architecture.png)
2. [figure2_rate.png](figures/figure2_rate.png)
3. [figure3_overrun.png](figures/figure3_overrun.png)
4. [figure4_admission.png](figures/figure4_admission.png)
5. [figure5_impact.png](figures/figure5_impact.png)
6. [figure6_feasibility.png](figures/figure6_feasibility.png)
