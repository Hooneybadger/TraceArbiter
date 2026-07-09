# Experiments

Unit tests (`ctest`) do not need tracefs write access.

A hardware campaign needs:

1. Group-writable tracefs (`scripts/setup_host.sh --apply` on this lab
   host; not chmod 777).
2. PARSEC native binaries and CloudSuite Data Caching Docker client.
   This tree reuses a local MetricTrust/CounterBouncer `vendor/`
   install; artifacts stay out of git.
3. Frozen budgets in `configs/budgets/` after calibration. Do not
   retune weights after holdout.

```sh
./build/tracearbiter preflight
python3 scripts/run_matrix.py --phase all --reps 5 \
  --campaign calibration-v1 --holdout-campaign native-holdout-v1
python3 analysis/analyze.py
```

Each `exec` JSON records kernel, CPU, command, plan, and buffer
stats. Published numbers are copied into `docs/results.md`. Raw
`artifacts/` JSON is local.
