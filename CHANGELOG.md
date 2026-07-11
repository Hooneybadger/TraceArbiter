# Changelog

Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning follows [SemVer](https://semver.org/) for the `tracearbiter`
CLI/library surface, not for measurement campaigns (those are named,
e.g. `native-holdout-v1`).

## [0.1.0] — 2026-07-11

### Added

- C++20 `tracearbiter` CLI: `preflight`, `discover`, `plan`, `exec`, `smoke`.
- Lexicographic admission planner (`critical` → `useful` → `bulk`) with
  explicit `CRITICAL_SOURCES_EXCEED_BUDGET`.
- Holdout `native-holdout-v1` on PARSEC native `blackscholes` /
  `swaptions` and CloudSuite Data Caching (165/165 OK).
- Unit tests for budget, planner, and tracefs parsing (no root).

### Observed

- On the evaluation host, ring-buffer capacity bound; full-set target
  overhead stayed under ~0.4%.
- Overrun dropped 9.1×–121× only when bulk `sys_enter` was not admitted.
  With the same six sources, static buffer split (B2) did not beat a
  single buffer (B1).
