# Limitations

See `docs/methodology.md` and the Negative results section of
`README.md`.

- Tracefs on this lab host is group-granted (`idblab`), not world
  writable. Reboot can drop that grant until `setup_host.sh --apply`
  is re-run.
- User-defined priorities are diagnosis-goal specific.
- Calibration assumes rate is stable enough for the same workload.
- Additive source MB/s is an estimator. On swaptions, `sys_enter`
  alone exceeded the measured full-set rate.
- Multi-instance buffer partitioning was an initial hypothesis. It
  did not show a consistent holdout win versus a single buffer with
  the same sources.
- CPU overhead was not binding here; do not claim overhead
  optimization as the result.
- REF is not ground truth. data-caching REF overran at 65536 KB/CPU.
- Snapshot event counts / REF are not an accuracy oracle.
- Kernel `dropped events` stayed 0; overrun is the overwrite-loss
  counter.
- PARSEC wall-time overhead is in the noise. CloudSuite wall time
  is a 30 s timeout.
- The 165-run campaign has no git commit. `artifacts/provenance/`
  checksums a later snapshot and must not be back-dated as the
  campaign SHA.
- TraceArbiter does not diagnose root cause.
- No distributed tracing and no CXL device.
- ftrace produces observer effect.
