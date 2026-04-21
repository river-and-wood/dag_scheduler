# C++ DAG 本地任务调度器 / C++ DAG Local Task Scheduler

一个可在 Debian 上运行的本地 DAG 工作流执行器。
A local DAG workflow executor for Debian.

核心能力 / Core capabilities:
- DAG 解析与环检测（Kahn） / DAG parsing and cycle detection (Kahn)
- 线程池并发执行 / Thread-pool based concurrent execution
- 任务状态机（Pending/Ready/Running/Retrying/Succeeded/Failed/TimedOut/Skipped） / Task state machine
- 失败传播与下游跳过 / Failure propagation and downstream skipping
- 重试与超时控制 / Retry and timeout handling
- 结构化日志、事件日志、Prometheus 文本指标 / Structured logs, event logs, and Prometheus text metrics
- CMake + Ninja 构建，CTest 自动化测试 / CMake + Ninja build with CTest automation

## 目录结构 / Project Structure

- `include/dag/`：模块接口 / module interfaces
- `src/`：模块实现 / module implementations
- `tests/`：端到端与核心逻辑测试 / e2e and core logic tests
- `examples/`：示例 workflow / sample workflows
- `packaging/dag-scheduler.service`：systemd 服务示例 / systemd service template
- `docs/ROADMAP.md`：项目里程碑与完成标准 / roadmap and acceptance criteria
- `docs/EXECUTION_PLAN.md`：逐步执行计划与验收项 / execution plan and verifications

## 项目文档 / Project Docs

- 路线图 / Roadmap: `docs/ROADMAP.md`
- 执行计划 / Execution plan: `docs/EXECUTION_PLAN.md`
- 部署手册 / Deployment guide: `docs/DEPLOYMENT.md`
- 发布清单 / Release checklist: `docs/RELEASE_CHECKLIST.md`
- 回滚方案 / Rollback plan: `docs/ROLLBACK.md`
- 变更记录 / Changelog: `CHANGELOG.md`

## 工作流格式 / Workflow Format

```text
workflow sample
fail_fast false

task task_id
  deps: dep_a, dep_b
  cmd: echo hello
  retries: 1
  timeout_ms: 3000
  priority: 10
  resource: default
end
```

字段说明 / Fields:
- `workflow`: 工作流名称 / workflow name
- `fail_fast`: 任一任务失败后是否尽快跳过后续任务 / skip remaining pending tasks after a failure
- `task`: 任务块开始 / task block start
- `deps`: 依赖任务列表（逗号分隔） / dependency list (comma-separated)
- `cmd`: 通过 `/bin/sh -c` 执行的命令 / command executed by `/bin/sh -c`
- `retries`: 最大重试次数 / max retry count
- `timeout_ms`: 超时毫秒，`0` 表示不限时 / timeout in milliseconds, `0` means no timeout
- `priority`: 就绪队列优先级（值越大优先级越高） / ready-queue priority (larger value = higher priority)
- `resource`: 资源类型（`default`/`cpu`/`io`） / resource class (`default`/`cpu`/`io`)
- `end`: 任务块结束 / task block end

## 构建 / Build

```bash
cmake --preset debug
cmake --build --preset debug
```

产物 / Output: `build/debug/dag_scheduler`

## 校验 workflow / Validate Workflow

```bash
./build/debug/dag_scheduler validate --workflow examples/sample.workflow
```

## 运行 workflow / Run Workflow

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

输出文件 / Output files:
- `report.json`: 每个任务最终状态和汇总 / final task states and run summary
- `metrics.prom`: Prometheus 文本指标 / Prometheus metrics text
- `events.jsonl`: 任务状态变更事件 / task state transition events
- `run.log`: 结构化运行日志 / structured run logs

`--resume` 行为 / `--resume` behavior:
- 会读取 `--events` 指向的历史事件日志并恢复终态任务，避免重复执行。  
  Reads the event log and restores terminal tasks to avoid re-running them.
- 恢复范围限制为同名 `workflow` 的最近一次运行；不同 workflow 的历史事件不会参与恢复。  
  Resume scope is limited to the latest run of the same `workflow`; events from other workflows are ignored.

`--max-cpu` / `--max-io`:
- 用于限制 `resource: cpu/io` 任务的并发数。  
  Limits concurrency of tasks tagged as `cpu`/`io`.

新增指标示例 / Additional metrics examples:
- `dag_queue_wait_total_ms` / `dag_queue_wait_max_ms` / `dag_queue_wait_avg_ms`
- `dag_failures_total{reason="non_zero_exit|signal|timed_out"}`

## 事件重放 / Event Replay

```bash
./build/debug/dag_scheduler replay --events events.jsonl
```

用途 / Purpose: 从事件日志重建任务最终状态摘要，快速判断是否存在失败或超时。  
Rebuilds final task-state summary from events to quickly detect failures/timeouts.

## 测试 / Tests

```bash
ctest --test-dir build/debug --output-on-failure
```

当前覆盖 / Current coverage:
- 有效 DAG 校验 / valid DAG validation
- 环检测 / cycle detection
- 依赖顺序执行 / dependency-order execution
- 失败后重试 / retry after failure
- 失败传播导致 `Skipped` / failure propagation to `Skipped`
- 超时语义 / timeout semantics
- `resume` 跨 workflow 隔离 / resume isolation across workflows

## Sanitizer

AddressSanitizer:
```bash
cmake --preset asan
cmake --build --preset asan
ctest --test-dir build/asan --output-on-failure
```

ThreadSanitizer:
```bash
cmake --preset tsan
cmake --build --preset tsan
ctest --test-dir build/tsan --output-on-failure
```

## systemd 部署示例 / systemd Deployment Example

参考 / Reference: `packaging/dag-scheduler.service`

按实际部署路径调整 `ExecStart`、`User`、日志目录后执行：  
Adjust `ExecStart`, `User`, and log paths for your environment, then run:

```bash
sudo cp packaging/dag-scheduler.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now dag-scheduler.service
sudo systemctl status dag-scheduler.service
journalctl -u dag-scheduler.service -f
```

## 模块说明 / Modules

- `WorkflowParser`: 解析 workflow 文本配置 / parses workflow text config
- `DagBuilder`: 构图、依赖合法性校验、环检测 / graph build, dependency checks, cycle detection
- `ThreadPoolExecutor`: 线程池执行并支持超时终止 / thread-pool execution with timeout termination
- `StateStore`: 任务状态机与事件记录 / task state store and event recording
- `EventReplayer`: 事件重放与恢复摘要 / event replay and recovery summary
- `Scheduler`: 依赖与优先级调度、失败传播、重试策略 / dependency+priority scheduling, failure propagation, retry policy
- `Observer`: 结构化日志、报告、指标输出 / structured log, report, and metrics output

## 后续增强建议 / Next Enhancements

1. 更强的崩溃恢复机制（例如进行中任务恢复） / stronger crash recovery (including in-flight tasks)
2. 更细粒度资源调度配额 / finer-grained resource scheduling quotas
3. 独立 metrics HTTP 导出端点 / dedicated metrics HTTP endpoint
4. 更完整失败策略（按任务 fail-open/fail-close） / richer failure policy (per-task fail-open/fail-close)
5. 接入 GoogleTest/Benchmark 并扩展 CI 质量门禁 / adopt GoogleTest/Benchmark and expand CI gates
