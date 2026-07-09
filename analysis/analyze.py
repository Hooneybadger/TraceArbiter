#!/usr/bin/env python3
"""Aggregate holdout JSONL into artifacts/analysis/report.json. No invented numbers."""

from __future__ import annotations

import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from ta_lib import BINARY, invoke_plan  # noqa: E402

JSONL = ROOT / "artifacts" / "native-holdout-v1" / "runs.jsonl"
CAL_JSONL = ROOT / "artifacts" / "calibration-v1" / "runs.jsonl"
OUT = ROOT / "artifacts" / "analysis" / "report.json"
CRITICAL_IDS = (
    "sched:sched_switch",
    "sched:sched_waking",
    "sched:sched_wakeup",
)
PRIORITY = {
    "sched:sched_switch": "critical",
    "sched:sched_waking": "critical",
    "sched:sched_wakeup": "critical",
    "sched:sched_process_exit": "useful",
    "exceptions:page_fault_user": "useful",
    "raw_syscalls:sys_enter": "bulk",
}
BASELINE = {"B1": "b1", "B2": "b2", "B3": "b3", "REF": "ref"}


def median(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    return statistics.median(xs)


def load_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def critical_count(row: dict) -> float | None:
    counts = row.get("critical_event_counts") or {}
    total = 0
    found = False
    for key in CRITICAL_IDS:
        if key in counts and counts[key] is not None:
            total += counts[key]
            found = True
    return total if found else None


def parse_plan_yaml(path: Path | None) -> dict:
    empty = {
        "feasible": None,
        "enabled": [],
        "n_critical": 0,
        "n_useful": 0,
        "n_bulk": 0,
        "total_buffer_kb": None,
    }
    if path is None or not Path(path).is_file():
        return empty
    enabled = []
    feasible = None
    total_buffer_kb = None
    current = None
    current_enabled = None
    for raw in Path(path).read_text().splitlines():
        line = raw.strip()
        if line.startswith("feasible:"):
            feasible = line.split(":", 1)[1].strip() == "true"
        elif line.startswith("total_buffer_kb:"):
            try:
                total_buffer_kb = int(line.split(":", 1)[1].strip())
            except ValueError:
                pass
        elif line.startswith('"') and line.endswith(":"):
            if current and current_enabled:
                enabled.append(current)
            current = line.strip('":')
            current_enabled = True
        elif line.startswith("enabled:") and current:
            current_enabled = line.split(":", 1)[1].strip() == "true"
    if current and current_enabled:
        enabled.append(current)
    counts = {"critical": 0, "useful": 0, "bulk": 0}
    for sid in enabled:
        counts[PRIORITY.get(sid, "bulk")] += 1
    return {
        "feasible": feasible,
        "enabled": enabled,
        "n_critical": counts["critical"],
        "n_useful": counts["useful"],
        "n_bulk": counts["bulk"],
        "total_buffer_kb": total_buffer_kb,
    }


def planner_meta(workload: str, budget: str, strategy: str) -> dict:
    empty = {
        "reasons": [],
        "estimated_trace_mb_per_sec": None,
        "estimated_overhead_pct": None,
    }
    baseline = BASELINE.get(strategy)
    if baseline is None:
        return empty
    cal = ROOT / "artifacts" / "calibration" / f"{workload}.yaml"
    budget_path = ROOT / "configs" / "budgets" / f"{budget}.yaml"
    if strategy == "REF":
        budget_path = ROOT / "configs" / "budgets" / "large.yaml"
    if not cal.is_file() or not budget_path.is_file() or not BINARY.is_file():
        return empty
    try:
        plan = invoke_plan(cal, budget_path, baseline)
    except Exception:
        return empty
    return {
        "reasons": list(plan.get("reasons") or []),
        "estimated_trace_mb_per_sec": plan.get("estimated_trace_mb_per_sec"),
        "estimated_overhead_pct": plan.get("estimated_overhead_pct"),
    }


def host_block() -> dict:
    path = ROOT / "artifacts" / "preflight" / "system.json"
    if not path.is_file():
        return {}
    raw = json.loads(path.read_text())
    return {
        "cpu_model": raw.get("cpu_model"),
        "kernel": raw.get("kernel"),
        "logical_cpus": raw.get("logical_cpus"),
        "physical_cores": raw.get("physical_cores"),
        "compiler": raw.get("compiler"),
        "git_commit": raw.get("git_commit") or None,
        "status": raw.get("status"),
        "tracefs_writable": (raw.get("tracefs") or {}).get("writable"),
    }


def main() -> int:
    holdout_raw = load_jsonl(JSONL)
    holdout = [
        r for r in holdout_raw
        if r.get("status") == "OK"
        and r.get("validated")
        and r.get("phase") == "holdout"
        and r.get("campaign") != "pipeline-smoke"
    ]
    if not holdout:
        print("analyze.py: no validated OK holdout rows", file=sys.stderr)
        return 2
    cal_rows = [
        r for r in load_jsonl(CAL_JSONL)
        if r.get("status") == "OK" and r.get("validated")
    ]
    report = {
        "campaign": "native-holdout-v1",
        "calibration_campaign": "calibration-v1",
        "n_holdout": len(holdout),
        "n_holdout_raw": len(holdout_raw),
        "n_calibration_ok": len(cal_rows),
        "git_commit": None,
        "host": host_block(),
        "critical_event_ids": list(CRITICAL_IDS),
        "primary_metrics": [
            "kernel_stats_overrun",
            "source_admission",
            "budget_feasibility",
            "application_impact",
        ],
        "retention_note": (
            "Snapshot event-count / REF is not an accuracy oracle. REF is "
            "not lossless on data-caching. Kernel stats overrun is the "
            "primary loss metric. dropped_events=0 with overrun>0 is the "
            "overwrite-mode ftrace accounting."
        ),
    }
    by = defaultdict(list)
    for row in holdout:
        by[(row["workload"], row["strategy"], row.get("budget"))].append(row)
    b0 = {}
    ref = {}
    for (workload, strategy, budget), group in by.items():
        if strategy == "B0":
            b0[workload] = group
        if strategy == "REF":
            ref[workload] = group
    tables = []
    composition = []
    freeze_path = ROOT / "artifacts" / "freeze" / "budgets.json"
    freeze_doc = json.loads(freeze_path.read_text()) if freeze_path.is_file() else {}
    for (workload, strategy, budget), group in sorted(by.items()):
        runtimes = [r.get("runtime_s") for r in group]
        overruns = [r.get("overrun") for r in group]
        bytes_ = [r.get("bytes") for r in group]
        p99s = [r.get("p99_latency_ms") for r in group if r.get("p99_latency_ms") is not None]
        rps = [r.get("throughput_rps") for r in group if r.get("throughput_rps") is not None]
        crits = [critical_count(r) for r in group]
        rates = []
        for row in group:
            if row.get("bytes") is not None and row.get("runtime_s"):
                rates.append(row["bytes"] / row["runtime_s"] / (1024.0 * 1024.0))
        base_rt = median([r.get("runtime_s") for r in b0.get(workload, [])])
        overhead = None
        if base_rt and median(runtimes) is not None and strategy != "B0":
            overhead = (median(runtimes) - base_rt) / base_rt * 100.0
        ref_c = median([critical_count(r) for r in ref.get(workload, [])])
        retention = None
        if ref_c and ref_c > 0 and median(crits) is not None:
            retention = median(crits) / ref_c
        base_p99 = median([r.get("p99_latency_ms") for r in b0.get(workload, []) if r.get("p99_latency_ms") is not None])
        p99_delta = None
        if base_p99 and median(p99s) is not None and strategy != "B0":
            p99_delta = (median(p99s) - base_p99) / base_p99 * 100.0
        plan_path = group[0].get("plan_path")
        plan = parse_plan_yaml(Path(plan_path) if plan_path else None)
        run_json_path = group[0].get("run_json")
        run_feasible = plan["feasible"]
        if run_json_path and Path(run_json_path).is_file():
            run_doc = json.loads(Path(run_json_path).read_text())
            if "feasible" in run_doc:
                run_feasible = run_doc["feasible"]
        meta = planner_meta(workload, budget if budget not in (None, "none") else "large", strategy)
        budget_cap = None
        if budget in (freeze_doc.get("budgets") or {}):
            budget_cap = freeze_doc["budgets"][budget].get("max_trace_mb_per_sec")
        tables.append({
            "workload": workload,
            "strategy": strategy,
            "budget": budget,
            "n": len(group),
            "runtime_s_median": median(runtimes),
            "overhead_pct_median": overhead,
            "overrun_median": median(overruns),
            "bytes_median": median(bytes_),
            "trace_mb_per_sec_median": median(rates),
            "p99_latency_ms_median": median(p99s) if p99s else None,
            "p99_delta_vs_b0_pct": p99_delta,
            "throughput_rps_median": median(rps) if rps else None,
            "critical_event_count_median": median(crits),
            "critical_retention_vs_ref": retention,
            "plan_feasible": run_feasible,
            "enabled_sources": plan["enabled"],
            "n_critical_enabled": plan["n_critical"],
            "n_useful_enabled": plan["n_useful"],
            "n_bulk_enabled": plan["n_bulk"],
            "plan_reasons": meta["reasons"],
            "estimated_trace_mb_per_sec": meta["estimated_trace_mb_per_sec"],
            "budget_max_trace_mb_per_sec": budget_cap,
        })
        composition.append({
            "workload": workload,
            "strategy": strategy,
            "budget": budget,
            "feasible": run_feasible,
            "reasons": meta["reasons"],
            "enabled": plan["enabled"],
            "n_critical": plan["n_critical"],
            "n_useful": plan["n_useful"],
            "n_bulk": plan["n_bulk"],
            "estimated_trace_mb_per_sec": meta["estimated_trace_mb_per_sec"],
            "budget_max_trace_mb_per_sec": budget_cap,
        })
    report["holdout_table"] = tables
    report["plan_composition"] = composition
    cal_sources = {}
    for path in sorted((ROOT / "artifacts" / "calibration").glob("*.json")):
        cal_sources[path.stem] = json.loads(path.read_text())
    report["calibration"] = cal_sources
    freeze = ROOT / "artifacts" / "freeze" / "budgets.json"
    if freeze.exists():
        report["freeze"] = json.loads(freeze.read_text())
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(str(OUT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
