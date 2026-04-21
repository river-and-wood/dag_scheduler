# Execution Plan (Step-by-Step)

## Current Baseline
- Build: `cmake --preset debug && cmake --build --preset debug`
- Test: `ctest --test-dir build/debug --output-on-failure`
- Current baseline status: passing (captured on 2026-04-21)

## Current Status
- M1 Planning Freeze: `done` (2026-04-21)
- M2 CI Setup: `done` (2026-04-21)
- M3 Coverage Gap Fill: `done` (2026-04-21)
- M4 Replay + Resume: `done` (2026-04-21)
- M5 Observability + Scheduling: `done` (2026-04-21)
- M6 Packaging + Release: `done` (2026-04-21)

## Step 1: Planning Freeze (M1)
- Tasks:
  - create roadmap and milestone acceptance criteria
  - define implementation sequence for next steps
- Output:
  - `docs/ROADMAP.md`
  - this file
- Verification:
  - team can trace each milestone to concrete deliverables

## Step 2: CI Setup (M2)
- Tasks:
  - add workflow for Linux Debug build + CTest
  - add ASAN/TSAN jobs
- Output:
  - `.github/workflows/ci.yml`
- Verification:
  - workflow succeeds on a clean run

## Step 3: Coverage Gap Fill (M3)
- Tasks:
  - add parser negative tests (duplicate IDs, missing deps, bad keys)
  - add scheduler fail_fast and retry/timeout corner tests
- Output:
  - updates in `tests/test_main.cpp` (or split test files)
- Verification:
  - all new tests fail before fix and pass after fix

## Step 4: Replay + Resume (M4)
- Tasks:
  - harden replay parser for malformed lines
  - implement `--resume` mode in CLI + scheduler bootstrap
- Output:
  - updates in `src/replay.cpp`, `src/main.cpp`, `src/scheduler.cpp`, related headers/tests
- Verification:
  - replay survives partial corruption
  - resumed run skips already succeeded tasks

## Step 5: Observability and Scheduling Enhancements (M5)
- Tasks:
  - add queue/wait/retry metrics and failure categories
  - implement basic resource-aware scheduling limits
- Output:
  - updates in observer/scheduler/state modules
- Verification:
  - new metrics appear in `metrics.prom`
  - scheduler behavior respects configured limits

## Step 6: Packaging + Release (M6)
- Tasks:
  - finalize deployment docs and service template usage
  - prepare changelog + versioning + release checklist
- Output:
  - `CHANGELOG.md`
  - docs updates in `README.md` and `packaging/`
- Verification:
  - local release gates passed:
    - `cmake --preset debug && cmake --build --preset debug && ctest --test-dir build/debug --output-on-failure`
    - `cmake --preset asan && cmake --build --preset asan && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan --output-on-failure`
    - `cmake --preset tsan && cmake --build --preset tsan && ctest --test-dir build/tsan --output-on-failure`
  - docs and packaging artifacts prepared for host deployment:
    - `docs/DEPLOYMENT.md`
    - `packaging/dag-scheduler.service`
    - `docs/RELEASE_CHECKLIST.md`

## Work Cadence
- Open one PR per step when feasible.
- Each PR must include:
  - code/tests/docs changes together
  - acceptance evidence (test output or example command)

## Tracking Template
Use this status block for each step:
- Status: `todo | in_progress | blocked | done`
- Owner: `<name>`
- Target date: `YYYY-MM-DD`
- Evidence: `<commands/logs/links>`
