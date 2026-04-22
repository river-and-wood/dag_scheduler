# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows Semantic Versioning.

## [Unreleased]

### Added
- None yet.

### Changed
- Default `run_id` generation is now collision-resistant for concurrent runs by using
  `milliseconds + pid + in-process sequence`.
- CLI numeric parsing is now strict for `--workers`, `--max-cpu`, and `--max-io`.
- Workflow numeric parsing is now strict and rejects trailing non-numeric characters.
- `--workers` now enforces a hard upper bound (`1024`) to avoid pathological resource requests.
- Resume startup scheduling now only initializes `Pending` zero-indegree tasks to avoid duplicate ready enqueue.
- Task execution now uses per-task process groups so timeout termination can target the full command subtree.

### Fixed
- Fixed `run_id` collisions when multiple `run` commands started within the same second.
- Fixed workflow command parsing where `#` inside quoted command strings could be truncated as comments.
- Fixed ambiguous runtime error path for `--workers -1` by adding explicit validation.
- Fixed `--workers` oversized input path that previously surfaced as `std::bad_alloc`.
- Fixed failure-reason metrics misclassification where plain non-zero exits such as `exit 130` were counted as `signal`.
- Fixed timeout cleanup so background child processes spawned by a task are terminated with the parent.
- Fixed `fail_fast` semantics during `--resume` so pending tasks are skipped when resumed state already contains failures.

## [0.2.0] - 2026-04-21

### Added
- Roadmap and execution plan documents under `docs/`.
- GitHub Actions CI for `debug`, `asan`, and `tsan` presets.
- `run --resume` support to continue from existing event logs.
- Workflow-level task resource class field: `resource: default|cpu|io`.
- Runtime resource limits via CLI: `--max-cpu` and `--max-io`.
- Queue wait and failure reason metrics in Prometheus output.
- New tests for parser validation, fail-fast behavior, replay robustness, resume, and resource limits.

### Changed
- Sanitizer flags in CMake are now passed as discrete compiler/linker flags.
- Event replay now tolerates malformed lines and unknown terminal details.

### Known Limitations
- Resume currently depends on event-log completeness and does not reconstruct partial in-flight task progress.
- Deployment runbook is documented, but clean-host validation remains a manual release gate.

## [0.1.0] - 2026-04-21

### Added
- Initial local DAG scheduler implementation (`validate`, `run`, `replay`).
- DAG validation, retry/timeout handling, fail propagation and skip semantics.
- JSON report output, structured logs, event logs, and Prometheus text metrics.
- CMake/Ninja build setup and end-to-end tests.
