# 执行计划（分步） / Execution Plan (Step-by-Step)

## 当前基线 / Current Baseline
- Build: `cmake --preset debug && cmake --build --preset debug`
- Test: `ctest --test-dir build/debug --output-on-failure`
- 当前基线状态：通过（2026-04-21）  
  Current baseline status: passing (2026-04-21)

## 当前状态 / Current Status
- M1 Planning Freeze: `done` (2026-04-21)
- M2 CI Setup: `done` (2026-04-21)
- M3 Coverage Gap Fill: `done` (2026-04-21)
- M4 Replay + Resume: `done` (2026-04-21)
- M5 Observability + Scheduling: `done` (2026-04-21)
- M6 Packaging + Release: `done` (2026-04-21)

## Step 1: 规划冻结（M1） / Planning Freeze (M1)
- Tasks:
- 制定路线图与里程碑验收标准 / create roadmap and milestone acceptance criteria
- 定义后续实现顺序 / define implementation sequence
- Output:
- `docs/ROADMAP.md`
- 本文件 / this file
- Verification:
- 团队可将每个里程碑映射到可交付物 / team can trace each milestone to concrete deliverables

## Step 2: CI 建设（M2） / CI Setup (M2)
- Tasks:
- 添加 Linux Debug + CTest workflow / add workflow for Linux Debug build + CTest
- 添加 ASAN/TSAN 任务 / add ASAN/TSAN jobs
- Output:
- `.github/workflows/ci.yml`
- Verification:
- 在干净环境可通过 / workflow succeeds on a clean run

## Step 3: 覆盖率缺口补齐（M3） / Coverage Gap Fill (M3)
- Tasks:
- 增加 parser 负例测试（重复 ID、缺失依赖、非法 key） / add parser negative tests
- 增加 scheduler fail_fast 与 retry/timeout 边界测试 / add fail_fast and retry/timeout corner tests
- Output:
- 更新 `tests/test_main.cpp`
- Verification:
- 新增测试在修复前失败、修复后通过 / new tests fail before fix and pass after fix

## Step 4: 回放与恢复（M4） / Replay + Resume (M4)
- Tasks:
- 强化 replay 对坏行容错 / harden replay parser for malformed lines
- 实现 `--resume` 与启动恢复逻辑 / implement `--resume` bootstrap
- Output:
- 更新 `src/replay.cpp`、`src/main.cpp`、`src/scheduler.cpp` 等
- Verification:
- 回放可忽略坏行 / replay survives partial corruption
- 恢复运行可跳过已成功任务 / resumed run skips already succeeded tasks

## Step 5: 可观测性与调度增强（M5） / Observability and Scheduling (M5)
- Tasks:
- 增加排队/重试/失败分类指标 / add queue/retry/failure-category metrics
- 实现基础资源约束调度 / implement resource-aware scheduling limits
- Output:
- 更新 observer/scheduler/state 模块
- Verification:
- `metrics.prom` 包含新指标 / new metrics appear in `metrics.prom`
- 调度行为符合资源上限 / scheduler respects configured limits

## Step 6: 打包与发布（M6） / Packaging + Release (M6)
- Tasks:
- 完成部署文档与服务模板 / finalize deployment docs and service template
- 完成 changelog、版本与发布清单 / prepare changelog, versioning, release checklist
- Output:
- `CHANGELOG.md`
- `README.md` 与 `packaging/` 更新
- Verification:
- 本地发布门禁通过 / local release gates passed:
- `cmake --preset debug && cmake --build --preset debug && ctest --test-dir build/debug --output-on-failure`
- `cmake --preset asan && cmake --build --preset asan && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan --output-on-failure`
- `cmake --preset tsan && cmake --build --preset tsan && ctest --test-dir build/tsan --output-on-failure`
- 已准备部署相关文档与模板 / deployment artifacts prepared:
- `docs/DEPLOYMENT.md`
- `packaging/dag-scheduler.service`
- `docs/RELEASE_CHECKLIST.md`

## 工作节奏 / Work Cadence
- 可行时按步骤拆 PR / open one PR per step when feasible.
- 每个 PR 需包含 / each PR must include:
- 代码、测试、文档同步更新 / code, tests, docs together
- 验收证据（命令输出或示例） / acceptance evidence (output/logs)

## 跟踪模板 / Tracking Template
- Status: `todo | in_progress | blocked | done`
- Owner: `<name>`
- Target date: `YYYY-MM-DD`
- Evidence: `<commands/logs/links>`
