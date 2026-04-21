# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog, and this project follows Semantic Versioning.

## [Unreleased]

### Added
- None yet.

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
