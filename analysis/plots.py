#!/usr/bin/env python3
"""Figures from artifacts/analysis/report.json only.

Figure mapping after holdout interpretation:
  1 architecture (SVG)
  2 per-source measured MB/s
  3 B1/B2/B3 overrun (log) - primary evidence
  4 B3 source admission
  5 application impact (negative: little movement)
  6 admitted MB/s vs budget cap
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "artifacts" / "analysis" / "report.json"
FIG = ROOT / "docs" / "figures"
ARTFIG = ROOT / "artifacts" / "figures"

SHORT = {
    "sched:sched_switch": "switch",
    "sched:sched_waking": "waking",
    "sched:sched_wakeup": "wakeup",
    "sched:sched_process_exit": "exit",
    "exceptions:page_fault_user": "fault",
    "raw_syscalls:sys_enter": "sys_enter",
    "_full": "full-set",
}


def load_report():
    if not REPORT.exists():
        print("plots.py: missing report.json", file=sys.stderr)
        return None
    return json.loads(REPORT.read_text())


def save(fig, name: str) -> None:
    FIG.mkdir(parents=True, exist_ok=True)
    ARTFIG.mkdir(parents=True, exist_ok=True)
    fig.savefig(FIG / name, dpi=140, bbox_inches="tight")
    fig.savefig(ARTFIG / name, dpi=140, bbox_inches="tight")


def pick(table, workload, strategy, budget, field):
    for row in table:
        if row["workload"] == workload and row["strategy"] == strategy and row["budget"] == budget:
            return row.get(field)
    return None


def main() -> int:
    report = load_report()
    if report is None:
        return 2
    try:
        import matplotlib.pyplot as plt
        import matplotlib
        matplotlib.use("Agg")
    except ImportError:
        print("plots.py: matplotlib missing", file=sys.stderr)
        return 2

    plt.rcParams.update({
        "axes.grid": True,
        "grid.alpha": 0.25,
        "font.size": 9,
    })

    table = report.get("holdout_table") or []
    if not table:
        print("plots.py: empty holdout table", file=sys.stderr)
        return 2

    workloads = ["blackscholes", "swaptions", "data-caching"]
    workloads = [w for w in workloads if any(r["workload"] == w for r in table)]
    strategies = ["B1", "B2", "B3"]
    budgets = ["small", "medium", "large"]
    colors = {"B1": "#4c72b0", "B2": "#dd8452", "B3": "#55a868"}

    # Figure 2 - measured MB/s; sys_enter is the volume hog
    cal = report.get("calibration") or {}
    names = [n for n in workloads if n in cal]
    fig, axes = plt.subplots(1, max(len(names), 1), figsize=(11, 3.6), squeeze=False)
    order = [
        "sched:sched_switch",
        "sched:sched_waking",
        "sched:sched_wakeup",
        "sched:sched_process_exit",
        "exceptions:page_fault_user",
        "raw_syscalls:sys_enter",
        "_full",
    ]
    bar_colors = []
    for k in order:
        if k == "raw_syscalls:sys_enter":
            bar_colors.append("#c44e52")
        elif k == "_full":
            bar_colors.append("#333333")
        else:
            bar_colors.append("#4c72b0")
    for col, name in enumerate(names):
        sources = cal[name].get("sources") or {}
        labels = [k for k in order if k in sources]
        xs = list(range(len(labels)))
        bps = [(sources[k].get("bytes_per_sec") or 0) / (1024 * 1024) for k in labels]
        cols = [bar_colors[order.index(k)] for k in labels]
        axes[0][col].bar(xs, bps, color=cols)
        axes[0][col].set_xticks(xs)
        axes[0][col].set_xticklabels([SHORT.get(k, k) for k in labels], rotation=30, ha="right")
        axes[0][col].set_ylabel("MB/s")
        axes[0][col].set_title(name)
    fig.suptitle("Calibration: per-source trace rate (sys_enter in red)")
    fig.tight_layout()
    save(fig, "figure2_rate.png")
    plt.close(fig)

    # Figure 3 - primary: B1 vs B2 vs B3 overrun
    fig, axes = plt.subplots(1, len(workloads), figsize=(11, 3.8), squeeze=False)
    x = list(range(len(budgets)))
    width = 0.25
    for col, workload in enumerate(workloads):
        ax = axes[0][col]
        for i, strategy in enumerate(strategies):
            ov = [pick(table, workload, strategy, b, "overrun_median") or 0 for b in budgets]
            xs = [j + i * width for j in x]
            ax.bar(xs, ov, width=width, color=colors[strategy], label=strategy)
        ax.set_xticks([j + width for j in x])
        ax.set_xticklabels(budgets)
        ax.set_yscale("log")
        ax.set_title(workload)
        ax.set_ylabel("median overrun (log)")
        ax.legend(fontsize=7)
    fig.suptitle("Holdout overrun: B1 single buffer vs B2 static split vs B3 admission")
    fig.tight_layout()
    save(fig, "figure3_overrun.png")
    plt.close(fig)

    # Figure 4 - B3 admission composition
    fig, axes = plt.subplots(1, len(workloads), figsize=(11, 3.8), squeeze=False)
    class_colors = {"critical": "#4c72b0", "useful": "#55a868", "bulk": "#c44e52"}
    for col, workload in enumerate(workloads):
        ax = axes[0][col]
        bottoms = [0, 0, 0]
        for cls, key in (("critical", "n_critical_enabled"), ("useful", "n_useful_enabled"), ("bulk", "n_bulk_enabled")):
            ys = [pick(table, workload, "B3", b, key) or 0 for b in budgets]
            ax.bar(budgets, ys, bottom=bottoms, color=class_colors[cls], label=cls)
            bottoms = [a + b for a, b in zip(bottoms, ys)]
        ax.set_ylim(0, 7)
        ax.set_ylabel("enabled sources")
        ax.set_title(f"{workload} B3")
        feas = [pick(table, workload, "B3", b, "plan_feasible") for b in budgets]
        for i, ok in enumerate(feas):
            if ok is False:
                ax.text(i, 6.2, "infeasible", ha="center", fontsize=7, color="#c44e52")
        ax.legend(fontsize=7)
    fig.suptitle("B3 admission (B1 and B2 always admit all six candidates)")
    fig.tight_layout()
    save(fig, "figure4_admission.png")
    plt.close(fig)

    # Figure 5 - application impact (mostly none)
    parsec = [w for w in workloads if w != "data-caching"]
    fig, axes = plt.subplots(1, max(len(parsec), 1) + 1, figsize=(11, 4.0), squeeze=False)
    width = 0.22
    x = list(range(len(budgets)))
    for col, w in enumerate(parsec):
        ax = axes[0][col]
        for i, strategy in enumerate(strategies):
            ys = [pick(table, w, strategy, b, "overhead_pct_median") or 0 for b in budgets]
            xs = [j + i * width for j in x]
            ax.bar(xs, ys, width=width, color=colors[strategy], label=strategy)
        ax.axhline(0, color="#333", linewidth=0.6)
        ax.set_xticks([j + width for j in x])
        ax.set_xticklabels(budgets)
        ax.set_ylabel("median runtime overhead % vs B0")
        ax.set_title(w)
        ax.legend(fontsize=7)
    ax = axes[0][-1]
    dc = "data-caching"
    series = ["B0"] + strategies
    series_color = {"B0": "#999999", **colors}
    b0_p99 = pick(table, dc, "B0", "none", "p99_latency_ms_median")
    width = 0.18
    for i, strategy in enumerate(series):
        if strategy == "B0":
            ys = [b0_p99] * len(budgets)
        else:
            ys = [pick(table, dc, strategy, b, "p99_latency_ms_median") for b in budgets]
        xs = [j + i * width for j in range(len(budgets))]
        ax.bar(xs, [0 if y is None else y for y in ys], width=width, color=series_color[strategy], label=strategy)
    ax.set_xticks([j + 1.5 * width for j in range(len(budgets))])
    ax.set_xticklabels(budgets)
    ax.set_ylabel("median interval p99 (ms)")
    ax.set_title("data-caching p99")
    ax.legend(fontsize=8)
    fig.suptitle("Application impact: tracing did not move outcomes much")
    fig.tight_layout()
    save(fig, "figure5_impact.png")
    plt.close(fig)

    # Figure 6 - B3 estimated admitted rate vs budget cap
    fig, axes = plt.subplots(1, len(workloads), figsize=(11, 3.8), squeeze=False)
    for col, workload in enumerate(workloads):
        ax = axes[0][col]
        admitted = [pick(table, workload, "B3", b, "estimated_trace_mb_per_sec") or 0 for b in budgets]
        caps = [pick(table, workload, "B3", b, "budget_max_trace_mb_per_sec") or 0 for b in budgets]
        ax.bar(budgets, admitted, color="#55a868", label="B3 estimated MB/s")
        ax.plot(budgets, caps, marker="D", color="#c44e52", label="budget cap")
        ax.set_ylabel("MB/s")
        ax.set_title(workload)
        ax.legend(fontsize=7)
        feas = [pick(table, workload, "B3", b, "plan_feasible") for b in budgets]
        for i, ok in enumerate(feas):
            if ok is False:
                ax.text(i, max(admitted[i], caps[i]) * 1.05 if max(admitted[i], caps[i]) else 0.1,
                        "infeasible", ha="center", fontsize=7, color="#c44e52")
    fig.suptitle("B3 budget feasibility: admitted source rate vs frozen MB/s cap")
    fig.tight_layout()
    save(fig, "figure6_feasibility.png")
    plt.close(fig)

    print(str(FIG))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
