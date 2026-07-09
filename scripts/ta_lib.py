"""Shared helpers for TraceArbiter experiment scripts."""

from __future__ import annotations

import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("TRACEARBITER_BIN", ROOT / "build" / "tracearbiter"))
PRIORITIES = ROOT / "configs" / "priorities.yaml"


def run(cmd: list[str], **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=False, text=True, **kwargs)


def write_plan_yaml(path: Path, plan: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        f"total_buffer_kb: {int(plan.get('total_buffer_kb') or 0)}",
        f"feasible: {str(bool(plan.get('feasible', True))).lower()}",
        "sources:",
    ]
    for row in plan.get("sources") or []:
        lines.append(f'  "{row["source"]}":')
        lines.append(f"    instance: {row['instance']}")
        lines.append(f"    enabled: {str(bool(row.get('enabled', True))).lower()}")
        lines.append(f"    buffer_kb: {int(row.get('buffer_kb') or 0)}")
    path.write_text("\n".join(lines) + "\n")


def single_source_plan(source_id: str, buffer_kb: int) -> dict:
    return {
        "total_buffer_kb": buffer_kb,
        "feasible": True,
        "sources": [
            {
                "source": source_id,
                "instance": "full",
                "enabled": True,
                "buffer_kb": buffer_kb,
            }
        ],
    }


def invoke_plan(priorities: Path, budget: Path, baseline: str) -> dict:
    proc = run(
        [
            str(BINARY),
            "plan",
            "--priorities",
            str(priorities),
            "--budget",
            str(budget),
            "--baseline",
            baseline,
        ],
        capture_output=True,
    )
    if not proc.stdout.strip():
        raise RuntimeError(proc.stderr)
    return json.loads(proc.stdout)


def invoke_exec(output: Path, command: list[str], plan: Path | None, workdir: str | None,
                accept_exit: list[int]) -> dict:
    output.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(BINARY), "exec", "--output", str(output)]
    if plan is None:
        cmd.append("--no-trace")
    else:
        cmd.extend(["--plan", str(plan)])
    if workdir:
        cmd.extend(["--workdir", workdir])
    if accept_exit != [0]:
        cmd.extend(["--accept-exit", ",".join(str(x) for x in accept_exit)])
    cmd.append("--")
    cmd.extend(command)
    proc = run(cmd, capture_output=True)
    if output.is_file():
        return json.loads(output.read_text())
    raise RuntimeError(proc.stderr or proc.stdout or "exec produced no output")


def append_jsonl(path: Path, row: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as fh:
        fh.write(json.dumps(row, sort_keys=True) + "\n")


def median(values: list[float]) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("median of empty")
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0
