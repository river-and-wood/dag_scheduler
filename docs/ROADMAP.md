# DAG Scheduler Roadmap

## Project Goal
Build a production-ready local DAG scheduler for Debian with reliable execution, replay/recovery, observability, CI quality gates, and reproducible release process.

## Definition of Done (DoD)
- Core commands `validate`, `run`, `replay` are stable on normal and failure cases.
- CI is green on main branch (build + tests + sanitizer jobs).
- Operation docs are complete (logs/events/metrics/systemd deployment).
- Release process is repeatable with versioning and changelog.

## Milestones

### M1: Baseline and Planning
- Scope: freeze current baseline, define delivery order and acceptance criteria.
- Deliverables:
  - `docs/ROADMAP.md`
  - `docs/EXECUTION_PLAN.md`
- Acceptance:
  - all steps have explicit output and measurable verification
  - execution order is fixed and prioritized

### M2: CI and Quality Guardrails
- Scope: set automated quality checks for every push/PR.
- Deliverables:
  - GitHub Actions workflow for Debug build + CTest
  - sanitizer workflow jobs (ASAN/TSAN)
- Acceptance:
  - CI runs automatically on push and pull request
  - failing checks block merge by policy

### M3: Test Coverage Hardening
- Scope: close coverage gaps in parser/scheduler failure paths.
- Deliverables:
  - new tests for duplicate task id, missing dependency, invalid keys, fail_fast behavior
  - regression tests for timeout/retry edge cases
- Acceptance:
  - critical edge/failure paths are covered by automated tests
  - at least one regression test exists per fixed bug

### M4: Replay Robustness and Resume
- Scope: improve replay tolerance and add resume capability.
- Deliverables:
  - robust parsing for partially corrupted JSONL event lines
  - `run --resume --events <path>` support
- Acceptance:
  - damaged lines do not crash replay
  - interrupted run can continue from prior event log state

### M5: Scheduling and Observability Enhancements
- Scope: improve resource-awareness and runtime visibility.
- Deliverables:
  - task-level resource constraints (for example cpu/io quota tags)
  - richer metrics (queue latency, retry rate, failure reason buckets)
  - optional metrics HTTP endpoint
- Acceptance:
  - high-concurrency runs stay stable under configured limits
  - metrics are directly consumable by Prometheus/Grafana

### M6: Packaging, Ops, and Release
- Scope: operational readiness and versioned release.
- Deliverables:
  - finalized deployment docs + systemd template
  - `CHANGELOG.md` and semantic versioning process
  - v1.0.0 release checklist and tag workflow
- Acceptance:
  - clean Debian host can deploy from docs end-to-end
  - tagged release can be reproduced from repository state

## Prioritization Rules
- Reliability before features.
- Tests before refactor.
- Observability before performance optimization.
- Release only after CI and deployment docs are complete.

## Proposed 2-Week Schedule
- Week 1: M1, M2, M3, M4
- Week 2: M5, M6

## Risk Register
- Risk: hidden race conditions in executor/scheduler concurrency.
  - Mitigation: TSAN in CI + targeted stress tests.
- Risk: replay format brittleness due manual JSON string parsing.
  - Mitigation: defensive parsing and malformed-line tests.
- Risk: production drift between docs and deploy reality.
  - Mitigation: deployment smoke test checklist in CI/manual gate.
