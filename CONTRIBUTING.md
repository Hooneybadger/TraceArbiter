# Contributing

This repository follows [GitHub Flow](https://docs.github.com/en/get-started/using-github/github-flow)
and [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/).

## Before you start

- Do not invent trace rates, overrun, or README performance numbers.
- Do not retune frozen `configs/budgets/*.yaml` after looking at holdout.
- Out of scope: profiler UI, custom ring buffer, eBPF-primary backend,
  LLM diagnosis, OpenTelemetry, or a CounterBouncer rewrite.
- Raw campaign JSON under `artifacts/` is local. Published tables live
  in `docs/results.md`. Reproduce with `experiments/README.md` on a
  machine you control.

## Development

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Unit tests do not need root or writable tracefs. Hardware ftrace
campaigns are not required for merge.

## Branches

Create a branch from `main`:

| Prefix | Use |
|---|---|
| `feat/<topic>` | user-visible behavior |
| `fix/<topic>` | bug fix |
| `docs/<topic>` | documentation only |
| `test/<topic>` | tests only |
| `chore/<topic>` | tooling, templates |
| `ci/<topic>` | workflow changes |

Examples: `feat/planner-cost`, `fix/tracefs-mode`, `docs/limitations`.

## Commits

```
<type>(<optional scope>): <imperative summary>
```

Types: `feat`, `fix`, `docs`, `test`, `refactor`, `chore`, `ci`.

- One concern per commit. A reviewer should be able to revert it safely.
- Explain *why* in the body when the diff is not obvious.
- Do not bundle unrelated files so the commit “looks complete”.

## Pull requests

1. Open an issue for behavior or policy changes when practical.
2. Push the branch and open a PR against `main` using the PR template.
3. Keep the PR scoped to one topic.
4. CI (`ctest`, GCC and Clang) must pass. Hardware ftrace is not
   required for merge.

## Issues

Use the issue forms under `.github/ISSUE_TEMPLATE/`. Attach redacted
`exec` JSON and plan YAML, not host secrets or sudo passwords.
