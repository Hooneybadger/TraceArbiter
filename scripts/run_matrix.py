#!/usr/bin/env python3
"""Calibration, budget freeze, and holdout matrix. Numbers come from exec artifacts."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from ta_lib import (  # noqa: E402
    BINARY,
    PRIORITIES,
    ROOT as REPO,
    append_jsonl,
    invoke_exec,
    invoke_plan,
    median,
    single_source_plan,
    write_plan_yaml,
)
from workloads import selected_workloads  # noqa: E402

ART = REPO / "artifacts"


def jsonl_path(campaign: str) -> Path:
    return ART / campaign / "runs.jsonl"


def load_priorities_ids() -> list[tuple[str, str]]:
    rows = []
    current = None
    for line in PRIORITIES.read_text().splitlines():
        stripped = line.strip()
        if stripped.startswith('"') and stripped.endswith(":"):
            current = stripped.strip('":')
        elif stripped.startswith("priority:") and current:
            rows.append((current, stripped.split(":", 1)[1].strip()))
            current = None
    return rows


def source_ids() -> list[str]:
    return [sid for sid, _ in load_priorities_ids()]


def write_calibrated_yaml(path: Path, measurements: dict, baseline_s: float) -> None:
    lines = [f"baseline_runtime_s: {baseline_s:.6f}", "sources:"]
    prio = dict(load_priorities_ids())
    for sid in source_ids():
        row = measurements.get(sid, {})
        lines.append(f'  "{sid}":')
        lines.append(f"    priority: {prio[sid]}")
        lines.append("    available: true")
        if "events_per_sec" in row:
            lines.append(f"    measured_events_per_sec: {row['events_per_sec']:.6f}")
        if "bytes_per_sec" in row:
            lines.append(f"    measured_bytes_per_sec: {row['bytes_per_sec']:.6f}")
        if "overhead_pct" in row:
            lines.append(f"    measured_overhead_pct: {row['overhead_pct']:.6f}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def exec_run(campaign: str, phase: str, workload, strategy: str, budget_name: str, rep: int,
             plan: dict | None, buffer_kb: int | None = None) -> dict:
    out_dir = ART / campaign / phase / workload.name / strategy / budget_name
    out_dir.mkdir(parents=True, exist_ok=True)
    output = out_dir / f"rep{rep}.json"
    plan_path = None
    if plan is not None:
        plan_path = out_dir / f"rep{rep}.plan.yaml"
        write_plan_yaml(plan_path, plan)
    result = invoke_exec(output, workload.command, plan_path, workload.workdir, workload.accept_exit)
    stdout = Path(str(output) + ".stdout")
    stderr = Path(str(output) + ".stderr")
    text = ""
    if stdout.exists():
        text += stdout.read_text(errors="replace")
    if stderr.exists():
        text += "\n" + stderr.read_text(errors="replace")
    parsed = workload.validate(text)
    row = {
        "campaign": campaign,
        "phase": phase,
        "workload": workload.name,
        "kind": workload.kind,
        "strategy": strategy,
        "budget": budget_name,
        "rep": rep,
        "status": result.get("status"),
        "runtime_s": result.get("runtime_s"),
        "entries": result.get("entries"),
        "overrun": result.get("overrun"),
        "bytes": result.get("bytes"),
        "dropped_events": result.get("dropped_events"),
        "exit_code": result.get("exit_code"),
        "validated": bool(parsed),
        "workload_metrics": parsed,
        "run_json": str(output),
        "plan_path": str(plan_path) if plan_path else None,
        "critical_event_counts": result.get("critical_event_counts"),
        "observations": result.get("observations"),
        "buffer_kb": buffer_kb,
    }
    if parsed and "p99_latency_ms" in parsed:
        row["p99_latency_ms"] = parsed["p99_latency_ms"]
        row["throughput_rps"] = parsed["throughput_rps"]
    append_jsonl(jsonl_path(campaign), row)
    (out_dir / f"rep{rep}.summary.json").write_text(json.dumps(row, indent=2, sort_keys=True) + "\n")
    return row


def load_jsonl() -> list[dict]:
    if not JSONL.exists():
        return []
    rows = []
    for line in JSONL.read_text().splitlines():
        if line.strip():
            rows.append(json.loads(line))
    return rows


def ok_rows(rows: list[dict], **filters) -> list[dict]:
    out = []
    for row in rows:
        if row.get("status") != "OK" or not row.get("validated"):
            continue
        if any(row.get(k) != v for k, v in filters.items()):
            continue
        out.append(row)
    return out


def calibrate(args) -> None:
    if not BINARY.is_file():
        raise SystemExit(f"missing binary {BINARY}")
    pre = run_preflight()
    if pre.get("status") != "READY":
        raise SystemExit(f"preflight {pre.get('status')}: {pre.get('tracefs', {}).get('reason')}")
    ids = source_ids()
    cal_buffer = args.cal_buffer_kb
    for workload in selected_workloads(include_cloudsuite=not args.parsec_only):
        print(f"== calibrate {workload.name} ==", flush=True)
        b0 = []
        for rep in range(args.reps):
            row = exec_run(args.campaign, "calibration", workload, "B0", "none", rep, None)
            print(f"  B0 rep{rep} runtime={row.get('runtime_s')} validated={row.get('validated')}", flush=True)
            b0.append(row)
        b0_ok = [r["runtime_s"] for r in b0 if r.get("validated") and r.get("runtime_s")]
        if len(b0_ok) < max(1, args.reps // 2):
            raise SystemExit(f"not enough B0 successes for {workload.name}")
        base = median(b0_ok)
        measured = {}
        for sid in ids:
            times = []
            bytes_s = []
            events_s = []
            plan = single_source_plan(sid, cal_buffer)
            for rep in range(args.reps):
                row = exec_run(args.campaign, "calibration", workload, f"src:{sid}", "cal", rep, plan,
                               buffer_kb=cal_buffer)
                print(f"  {sid} rep{rep} runtime={row.get('runtime_s')} bytes={row.get('bytes')}", flush=True)
                if not row.get("validated") or not row.get("runtime_s"):
                    continue
                rt = row["runtime_s"]
                times.append(rt)
                if row.get("bytes"):
                    bytes_s.append(row["bytes"] / rt)
                counts = row.get("critical_event_counts") or {}
                if sid in counts:
                    events_s.append(counts[sid] / rt)
                elif row.get("entries"):
                    events_s.append(row["entries"] / rt)
            if not times:
                continue
            rt = median(times)
            measured[sid] = {
                "overhead_pct": (rt - base) / base * 100.0,
                "bytes_per_sec": median(bytes_s) if bytes_s else 0.0,
                "events_per_sec": median(events_s) if events_s else 0.0,
            }
        full_plan = invoke_plan(PRIORITIES, REPO / "configs/budgets/medium.yaml", "b1")
        # Use calibration buffer for full-set cost, not the placeholder medium budget.
        for src in full_plan.get("sources") or []:
            src["buffer_kb"] = cal_buffer
        full_plan["total_buffer_kb"] = cal_buffer
        full_times = []
        full_bytes = []
        for rep in range(args.reps):
            row = exec_run(args.campaign, "calibration", workload, "B1", "cal", rep, full_plan,
                           buffer_kb=cal_buffer)
            print(f"  B1-cal rep{rep} runtime={row.get('runtime_s')}", flush=True)
            if row.get("validated") and row.get("runtime_s"):
                full_times.append(row["runtime_s"])
                if row.get("bytes"):
                    full_bytes.append(row["bytes"] / row["runtime_s"])
        measured["_full"] = {
            "overhead_pct": (median(full_times) - base) / base * 100.0 if full_times else 0.0,
            "bytes_per_sec": median(full_bytes) if full_bytes else 0.0,
        }
        ref_plan = invoke_plan(PRIORITIES, REPO / "configs/budgets/large.yaml", "ref")
        for src in ref_plan.get("sources") or []:
            src["buffer_kb"] = args.ref_buffer_kb
        ref_plan["total_buffer_kb"] = args.ref_buffer_kb
        for rep in range(args.reps):
            row = exec_run(args.campaign, "calibration", workload, "REF", "ref", rep, ref_plan,
                           buffer_kb=args.ref_buffer_kb)
            print(f"  REF rep{rep} runtime={row.get('runtime_s')} overrun={row.get('overrun')}", flush=True)
        out = ART / "calibration" / f"{workload.name}.yaml"
        write_calibrated_yaml(out, measured, base)
        meta = {"workload": workload.name, "baseline_runtime_s": base, "sources": measured}
        (ART / "calibration" / f"{workload.name}.json").write_text(json.dumps(meta, indent=2, sort_keys=True) + "\n")
        print(f"  wrote {out}", flush=True)


def run_preflight() -> dict:
    out = ART / "preflight" / "system.json"
    from ta_lib import run
    proc = run([str(BINARY), "preflight", "--output", str(out)], capture_output=True)
    if out.is_file():
        return json.loads(out.read_text())
    raise RuntimeError(proc.stderr)


def freeze(args) -> None:
    full_overheads = []
    full_bytes = []
    for path in sorted((ART / "calibration").glob("*.json")):
        data = json.loads(path.read_text())
        full = data.get("sources", {}).get("_full", {})
        if "overhead_pct" in full:
            full_overheads.append(full["overhead_pct"])
        if "bytes_per_sec" in full:
            full_bytes.append(full["bytes_per_sec"])
    if not full_overheads:
        raise SystemExit("freeze needs calibration JSON")
    max_oh = max(full_overheads)
    med_oh = median(full_overheads)
    max_bps = max(full_bytes) if full_bytes else 1.0
    # Budgets span observed full-set cost. small is tighter than measured full
    # overhead; large leaves headroom. Buffer sizes are per-CPU tracefs units
    # chosen from observed bytes/sec (not from holdout outcomes).
    small_oh = max(0.25, med_oh * 0.5)
    medium_oh = max(small_oh * 1.5, med_oh)
    large_oh = max(medium_oh * 1.5, max_oh * 1.25, medium_oh + 0.5)
    mb_s = max_bps / (1024.0 * 1024.0)
    small_mb = max(0.5, mb_s * 0.5)
    medium_mb = max(small_mb * 2.0, mb_s)
    large_mb = max(medium_mb * 2.0, mb_s * 1.5)
    small_buf = 256
    medium_buf = 2048
    large_buf = 16384
    freeze_doc = {
        "campaign": args.campaign,
        "method": "half/median/1.25x of observed full-set calibration overhead and bytes/sec",
        "observed_full_overhead_pct": full_overheads,
        "observed_full_bytes_per_sec": full_bytes,
        "budgets": {
            "small": {"buffer_kb": small_buf, "max_target_overhead_pct": small_oh, "max_trace_mb_per_sec": small_mb},
            "medium": {"buffer_kb": medium_buf, "max_target_overhead_pct": medium_oh, "max_trace_mb_per_sec": medium_mb},
            "large": {"buffer_kb": large_buf, "max_target_overhead_pct": large_oh, "max_trace_mb_per_sec": large_mb},
        },
        "weights": {"trace_rate": 1.0, "overhead": 1.0},
        "critical_buffer_floor": 0.5,
        "static_split": "B2 keeps 70% buffer on critical instance, 30% on bulk",
        "reference_buffer_kb": args.ref_buffer_kb,
    }
    freeze_dir = ART / "freeze"
    freeze_dir.mkdir(parents=True, exist_ok=True)
    (freeze_dir / "budgets.json").write_text(json.dumps(freeze_doc, indent=2, sort_keys=True) + "\n")
    for name, spec in freeze_doc["budgets"].items():
        text = (
            f"# Frozen from {args.campaign} calibration. Do not retune after holdout.\n"
            f"buffer_kb: {spec['buffer_kb']}\n"
            f"max_target_overhead_pct: {spec['max_target_overhead_pct']:.6f}\n"
            f"max_trace_mb_per_sec: {spec['max_trace_mb_per_sec']:.6f}\n"
            "critical_buffer_floor: 0.5\n"
            "weights:\n"
            "  trace_rate: 1.0\n"
            "  overhead: 1.0\n"
        )
        (REPO / "configs" / "budgets" / f"{name}.yaml").write_text(text)
        (freeze_dir / f"{name}.yaml").write_text(text)
    (REPO / "experiments" / "manifest.yaml").write_text(
        "campaign_id: native-holdout-v1\n"
        "status: frozen\n"
        f"calibration_campaign: {args.campaign}\n"
        "workloads:\n"
        "  - blackscholes\n"
        "  - swaptions\n"
        "  - data-caching\n"
        "budgets: [small, medium, large]\n"
        "baselines: [B0, B1, B2, B3]\n"
        "reference: critical-only large buffer\n"
        "reps: 5\n"
        "note: Do not retune planner weights after this freeze.\n"
    )
    print(json.dumps(freeze_doc["budgets"], indent=2))


def holdout(args) -> None:
    freeze_path = ART / "freeze" / "budgets.json"
    if not freeze_path.is_file():
        raise SystemExit("run freeze first")
    budgets = ["small", "medium", "large"]
    for workload in selected_workloads(include_cloudsuite=not args.parsec_only):
        cal = ART / "calibration" / f"{workload.name}.yaml"
        if not cal.is_file():
            raise SystemExit(f"missing {cal}")
        print(f"== holdout {workload.name} ==", flush=True)
        for rep in range(args.reps):
            row = exec_run(args.holdout_campaign, "holdout", workload, "B0", "none", rep, None)
            print(f"  B0 none rep{rep} rt={row.get('runtime_s')} ok={row.get('validated')}", flush=True)
        ref_plan = invoke_plan(cal, REPO / "configs/budgets/large.yaml", "ref")
        for src in ref_plan.get("sources") or []:
            src["buffer_kb"] = args.ref_buffer_kb
        ref_plan["total_buffer_kb"] = args.ref_buffer_kb
        for rep in range(args.reps):
            row = exec_run(args.holdout_campaign, "holdout", workload, "REF", "ref", rep, ref_plan,
                           buffer_kb=args.ref_buffer_kb)
            print(f"  REF rep{rep} rt={row.get('runtime_s')} ov={row.get('overrun')} ok={row.get('validated')}",
                  flush=True)
        for budget_name in budgets:
            budget = REPO / "configs/budgets" / f"{budget_name}.yaml"
            plans = {
                "B1": invoke_plan(cal, budget, "b1"),
                "B2": invoke_plan(cal, budget, "b2"),
                "B3": invoke_plan(cal, budget, "b3"),
            }
            for strategy, plan in plans.items():
                for rep in range(args.reps):
                    row = exec_run(args.holdout_campaign, "holdout", workload, strategy, budget_name,
                                   rep, plan, buffer_kb=plan.get("total_buffer_kb"))
                    print(
                        f"  {strategy} {budget_name} rep{rep} "
                        f"rt={row.get('runtime_s')} ov={row.get('overrun')} ok={row.get('validated')}",
                        flush=True,
                    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=["calibrate", "freeze", "holdout", "all"], default="all")
    parser.add_argument("--reps", type=int, default=5)
    parser.add_argument("--campaign", default="calibration-v1")
    parser.add_argument("--holdout-campaign", default="native-holdout-v1")
    parser.add_argument("--cal-buffer-kb", type=int, default=4096)
    parser.add_argument("--ref-buffer-kb", type=int, default=65536)
    parser.add_argument("--parsec-only", action="store_true")
    args = parser.parse_args()
    ART.mkdir(exist_ok=True)
    if args.phase in ("calibrate", "all"):
        calibrate(args)
    if args.phase in ("freeze", "all"):
        freeze(args)
    if args.phase in ("holdout", "all"):
        holdout(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
