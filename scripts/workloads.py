"""Host workload adapters. Reuse vendor installs; keep artifacts in TraceArbiter."""

from __future__ import annotations

import csv
import math
import os
import statistics
from dataclasses import dataclass
from pathlib import Path

def _parsec_root() -> Path:
    env = os.environ.get("TRACEARBITER_PARSEC")
    if env:
        return Path(env)
    parent = Path(__file__).resolve().parents[1].parent
    for name in ("CounterBouncer", "MetricTrust"):
        candidate = parent / name / "vendor" / "parsec"
        if candidate.exists():
            return candidate
    return parent / "CounterBouncer" / "vendor" / "parsec"


PARSEC_ROOT = _parsec_root()
PIN = os.environ.get("TRACEARBITER_PIN", "2,4,6,8")
LAYOUT = {
    "blackscholes": "apps",
    "swaptions": "apps",
    "canneal": "kernels",
    "freqmine": "apps",
    "streamcluster": "kernels",
}
ARGS = {
    "blackscholes": ["4", "in_10M.txt", "prices.txt"],
    "swaptions": ["-ns", "128", "-sm", "1000000", "-nt", "4"],
    "canneal": ["4", "15000", "2000", "2500000.nets", "6000"],
    "freqmine": ["webdocs_250k.dat", "11000"],
    "streamcluster": ["10", "20", "128", "1000000", "200000", "5000", "none", "output.txt", "4"],
}


@dataclass
class Workload:
    name: str
    kind: str
    command: list[str]
    workdir: str | None
    accept_exit: list[int]
    validate: callable


def parse_parsec(name: str, text: str) -> dict | None:
    if "PARSEC Benchmark Suite" not in text:
        return None
    if name == "blackscholes" and "Num of Options: 10000000" not in text:
        return None
    if name == "swaptions":
        if "Number of Simulations:" not in text or "SwaptionPrice:" not in text:
            return None
        if "Fewer swaptions than threads" in text:
            return None
    if name == "canneal" and "Final routing is:" not in text:
        return None
    if name == "freqmine" and "the FPgrowth cost" not in text:
        return None
    return {"validated_program_output": True}


def parsec(name: str) -> Workload:
    package = PARSEC_ROOT / "pkgs" / LAYOUT[name] / name
    binary = package / "inst/amd64-linux.gcc/bin" / name
    if not binary.is_file():
        raise FileNotFoundError(binary)
    workdir = package / "run/tracearbiter-native"
    src = package / "run/counterbouncer-native"
    if not workdir.exists() and src.exists():
        workdir = src
    workdir.mkdir(parents=True, exist_ok=True)
    command = [
        "env",
        "OMP_NUM_THREADS=4",
        "LC_ALL=C",
        "taskset",
        "-c",
        PIN,
        str(binary),
        *ARGS[name],
    ]
    return Workload(name, "parsec", command, str(workdir), [0],
                    lambda text, n=name: parse_parsec(n, text))


def parse_cloudsuite(text: str) -> dict | None:
    rows = []
    for line in text.splitlines():
        try:
            fields = [float(x.strip()) for x in next(csv.reader([line]))]
        except (ValueError, StopIteration):
            continue
        if len(fields) == 16 and fields[0] > 1_000_000_000 and fields[1] > 0:
            if all(math.isfinite(fields[i]) and fields[i] >= 0 for i in (2, 3, 8, 11)):
                rows.append(fields)
    steady = rows[2:]
    if len(steady) < 5:
        return None
    duration = sum(r[1] for r in steady)
    return {
        "throughput_rps": sum(r[3] for r in steady) / duration,
        "p99_latency_ms": statistics.median(r[11] for r in steady),
        "interval_count": len(steady),
    }


def cloudsuite(duration_s: int = 30, rps: int = 100000) -> Workload:
    client = os.environ.get("TRACEARBITER_DC_CLIENT", "metrictrust-dc-client")
    command = [
        "docker",
        "exec",
        "-t",
        client,
        "timeout",
        "--signal=TERM",
        str(duration_s),
        "/bin/bash",
        "/entrypoint.sh",
        "--m=RPS",
        "--S=28",
        "--g=0.8",
        "--c=200",
        "--w=8",
        "--T=1",
        f"--r={rps}",
    ]
    return Workload(
        "data-caching",
        "cloudsuite",
        command,
        None,
        [0, 124],
        parse_cloudsuite,
    )


def selected_workloads(include_cloudsuite: bool = True) -> list[Workload]:
    items = [parsec("blackscholes"), parsec("swaptions")]
    if include_cloudsuite:
        items.append(cloudsuite())
    return items
