#!/usr/bin/env python3
"""Hash the current source snapshot. Does not claim this SHA ran holdout."""

from __future__ import annotations

import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts" / "provenance"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def collect_paths() -> list[Path]:
    globs = [
        "CMakeLists.txt",
        ".github/workflows/ci.yml",
        "include/tracearbiter/**/*.hpp",
        "src/**/*.cpp",
        "src/**/*.hpp",
        "tests/**/*.cpp",
        "configs/**/*.yaml",
        "scripts/*.py",
        "scripts/*.sh",
        "analysis/*.py",
        "experiments/manifest.yaml",
        "artifacts/freeze/budgets.json",
        "artifacts/calibration/*.yaml",
        "artifacts/calibration/*.json",
        "artifacts/native-holdout-v1/runs.jsonl",
        "artifacts/calibration-v1/runs.jsonl",
    ]
    paths: list[Path] = []
    for pattern in globs:
        paths.extend(sorted(ROOT.glob(pattern)))
    binary = ROOT / "build" / "tracearbiter"
    if binary.is_file():
        paths.append(binary)
    # unique, skip venv
    out = []
    seen = set()
    for path in paths:
        if ".venv" in path.parts or path.name.endswith(".pyc"):
            continue
        rel = path.relative_to(ROOT)
        if rel in seen:
            continue
        seen.add(rel)
        out.append(path)
    return out


def file_id(path: Path) -> str:
    try:
        return subprocess.check_output(["file", "-b", str(path)], text=True).strip()
    except Exception:
        return ""


def compiler_id() -> str:
    try:
        return subprocess.check_output(["g++", "-dumpversion"], text=True).strip()
    except Exception:
        return ""


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    files = []
    concat = hashlib.sha256()
    for path in collect_paths():
        digest = sha256_file(path)
        rel = str(path.relative_to(ROOT))
        files.append({
            "path": rel,
            "sha256": digest,
            "size": path.stat().st_size,
        })
        concat.update(rel.encode())
        concat.update(b"\0")
        concat.update(bytes.fromhex(digest))
        concat.update(b"\n")
    files.sort(key=lambda row: row["path"])
    doc = {
        "recorded_at_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "campaign": "native-holdout-v1",
        "calibration_campaign": "calibration-v1",
        "campaign_git_commit": None,
        "note": (
            "Holdout native-holdout-v1 was executed from an uncommitted source "
            "snapshot. This file checksums the tree after that campaign, once "
            "documentation was rewritten to match holdout interpretation. It is "
            "not a claim that a git SHA produced the 165 holdout runs."
        ),
        "tree_sha256": concat.hexdigest(),
        "compiler_gxx_dumpversion": compiler_id(),
        "binary": None,
        "files": files,
    }
    binary = ROOT / "build" / "tracearbiter"
    if binary.is_file():
        doc["binary"] = {
            "path": "build/tracearbiter",
            "sha256": sha256_file(binary),
            "size": binary.stat().st_size,
            "file": file_id(binary),
            "mtime": datetime.fromtimestamp(binary.stat().st_mtime, timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        }
    (OUT / "snapshot.json").write_text(json.dumps(doc, indent=2, sort_keys=True) + "\n")
    lines = [
        "# Provenance snapshot",
        "",
        doc["note"],
        "",
        f"- recorded_at_utc: {doc['recorded_at_utc']}",
        f"- tree_sha256: `{doc['tree_sha256']}`",
        f"- campaign_git_commit: none",
        "",
    ]
    if doc["binary"]:
        lines.append(f"- binary sha256: `{doc['binary']['sha256']}`")
        lines.append("")
    (OUT / "SNAPSHOT.md").write_text("\n".join(lines) + "\n")
    print(OUT / "snapshot.json")
    print("tree_sha256", doc["tree_sha256"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
