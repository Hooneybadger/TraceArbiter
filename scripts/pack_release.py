#!/usr/bin/env python3
"""Pack campaign evidence for the v0.1.0 GitHub Release. Does not rewrite artifacts."""

from __future__ import annotations

import json
import shutil
import subprocess
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGE = ROOT / "artifacts/release/tracearbiter-v0.1.0-results"
ARCHIVE = ROOT / "artifacts/release/tracearbiter-v0.1.0-results.tar.zst"


def copy_file(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def sha256sums() -> None:
    digest = subprocess.check_output(
        ["bash", "-c", "find . -type f ! -name SHA256SUMS -print0 | sort -z | xargs -0 sha256sum"],
        cwd=STAGE,
        text=True,
    )
    (STAGE / "SHA256SUMS").write_text(digest)


def main() -> int:
    if STAGE.exists():
        shutil.rmtree(STAGE)
    STAGE.mkdir(parents=True)

    for rel in (
        "experiments/manifest.yaml",
        "configs/budgets/small.yaml",
        "configs/budgets/medium.yaml",
        "configs/budgets/large.yaml",
        "configs/priorities.yaml",
        "configs/sources.yaml",
        "configs/experiments.yaml",
    ):
        copy_file(ROOT / rel, STAGE / rel)

    copy_file(ROOT / "artifacts/analysis/report.json", STAGE / "report.json")
    copy_file(ROOT / "artifacts/freeze/budgets.json", STAGE / "freeze/budgets.json")
    copy_file(ROOT / "artifacts/native-holdout-v1/runs.jsonl", STAGE / "raw/native-holdout-v1/runs.jsonl")
    copy_file(ROOT / "artifacts/calibration-v1/runs.jsonl", STAGE / "raw/calibration-v1/runs.jsonl")
    copy_file(ROOT / "artifacts/preflight/system.json", STAGE / "preflight/system.json")
    copy_file(ROOT / "artifacts/provenance/snapshot.json", STAGE / "provenance.json")
    copy_file(ROOT / "artifacts/provenance/SNAPSHOT.md", STAGE / "SNAPSHOT.md")
    for path in sorted((ROOT / "artifacts/calibration").glob("*.json")):
        copy_file(path, STAGE / "calibration" / path.name)
    for path in sorted((ROOT / "artifacts/calibration").glob("*.yaml")):
        copy_file(path, STAGE / "calibration" / path.name)

    n_holdout = sum(1 for _ in (ROOT / "artifacts/native-holdout-v1/runs.jsonl").open())
    n_cal = sum(1 for _ in (ROOT / "artifacts/calibration-v1/runs.jsonl").open())
    note = {
        "campaign": "native-holdout-v1",
        "calibration_campaign": "calibration-v1",
        "n_holdout_jsonl": n_holdout,
        "n_calibration_jsonl": n_cal,
        "campaign_git_commit": None,
        "note": (
            "Holdout ran from an uncommitted snapshot. provenance.json "
            "checksums the tree after documentation freeze. Do not treat "
            "the published v0.1.0 tag as the execution SHA."
        ),
    }
    (STAGE / "git-commits.json").write_text(json.dumps(note, indent=2) + "\n")
    (STAGE / "README.txt").write_text(
        "TraceArbiter v0.1.0 campaign evidence\n"
        "\n"
        "Aggregates and raw JSONL for calibration-v1 and native-holdout-v1.\n"
        "Not in git because the public tree stays reviewable.\n"
        "\n"
        "campaign_git_commit is null. See SNAPSHOT.md and git-commits.json.\n"
        "Per-rep exec JSON and stdout stay on the lab host.\n"
    )
    sha256sums()

    ARCHIVE.parent.mkdir(parents=True, exist_ok=True)
    tar_path = ARCHIVE.with_suffix("")
    if tar_path.exists():
        tar_path.unlink()
    if ARCHIVE.exists():
        ARCHIVE.unlink()
    with tarfile.open(tar_path, "w") as tar:
        tar.add(STAGE, arcname=STAGE.name)
    subprocess.check_call(["zstd", "-f", "-19", str(tar_path)])
    tar_path.unlink(missing_ok=True)
    print(ARCHIVE, ARCHIVE.stat().st_size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
