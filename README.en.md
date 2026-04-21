# C++ DAG Local Task Scheduler

<p align="center">
  <a href="./README.zh.md"><img alt="简体中文" src="https://img.shields.io/badge/%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-Switch-0f172a"></a>
  <a href="./README.en.md"><img alt="English (Current)" src="https://img.shields.io/badge/English-Current-1677ff"></a>
</p>

A local DAG workflow executor for Debian.

## Core Capabilities
- DAG parsing and cycle detection (Kahn)
- Thread-pool based concurrent execution
- Task state machine (`Pending/Ready/Running/Retrying/Succeeded/Failed/TimedOut/Skipped`)
- Failure propagation and downstream skipping
- Retry and timeout handling
- Structured logs, event logs, Prometheus text metrics
- CMake + Ninja build with CTest automation

## Project Structure
- `include/dag/`: module interfaces
- `src/`: module implementations
- `tests/`: e2e and core logic tests
- `examples/`: sample workflows
- `packaging/dag-scheduler.service`: systemd service template

## Quick Start

Build:
```bash
cmake --preset debug
cmake --build --preset debug
```

Validate:
```bash
./build/debug/dag_scheduler validate --workflow examples/sample.workflow
```

Run:
```bash
./build/debug/dag_scheduler run \
  --workflow examples/sample.workflow \
  --workers 4 \
  --max-cpu 2 \
  --max-io 2 \
  --report report.json \
  --metrics metrics.prom \
  --events events.jsonl \
  --log run.log \
  --resume
```

Replay:
```bash
./build/debug/dag_scheduler replay --events events.jsonl
```

Tests:
```bash
ctest --test-dir build/debug --output-on-failure
```

## Resume Behavior
- Restores terminal task states from `--events` to avoid re-running completed tasks.
- Resume scope is limited to the latest run of the same `workflow`.
- Workflow fingerprint must match; mismatched historical events are ignored.

## Docs
- Roadmap: `docs/ROADMAP.md`
- Execution plan: `docs/EXECUTION_PLAN.md`
- Deployment guide: `docs/DEPLOYMENT.md`
- Release checklist: `docs/RELEASE_CHECKLIST.md`
- Rollback plan: `docs/ROLLBACK.md`
- Changelog: `CHANGELOG.md`
