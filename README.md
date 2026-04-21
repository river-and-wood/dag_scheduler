# C++ DAG 本地任务调度器

一个可在 Debian 上运行的本地 DAG 工作流执行器，包含：
- DAG 解析与环检测（Kahn）
- 线程池并发执行
- 任务状态机（Pending/Ready/Running/Retrying/Succeeded/Failed/TimedOut/Skipped）
- 失败传播与跳过下游
- 重试和超时
- 结构化日志、事件日志、Prometheus 文本指标
- CMake + Ninja 构建，CTest 自动化测试

## 目录结构

- `include/dag/`：模块接口
- `src/`：模块实现
- `tests/`：端到端与核心逻辑测试
- `examples/`：示例 workflow
- `packaging/dag-scheduler.service`：systemd 服务示例

## 工作流格式

```text
workflow sample
fail_fast false

task task_id
  deps: dep_a, dep_b
  cmd: echo hello
  retries: 1
  timeout_ms: 3000
  priority: 10
end
```

字段说明：
- `workflow`: 工作流名称
- `fail_fast`: 任一任务失败后是否尽快跳过后续待执行任务
- `task`: 任务块开始
- `deps`: 依赖任务列表（逗号分隔）
- `cmd`: 通过 `/bin/sh -c` 执行的命令
- `retries`: 最大重试次数
- `timeout_ms`: 超时毫秒，`0` 表示不限时
- `priority`: 就绪队列优先级（数值越大优先级越高）
- `end`: 任务块结束

## 构建

```bash
cmake --preset debug
cmake --build --preset debug
```

产物：`build/debug/dag_scheduler`

## 验证 workflow

```bash
./build/debug/dag_scheduler validate --workflow examples/sample.workflow
```

## 运行 workflow

```bash
./build/debug/dag_scheduler run \
  --workflow examples/sample.workflow \
  --workers 4 \
  --report report.json \
  --metrics metrics.prom \
  --events events.jsonl \
  --log run.log
```

输出：
- `report.json`: 每个任务最终状态和汇总
- `metrics.prom`: Prometheus 文本指标
- `events.jsonl`: 任务状态变更事件日志
- `run.log`: 结构化运行日志

## 事件重放（恢复摘要）

```bash
./build/debug/dag_scheduler replay --events events.jsonl
```

用途：从事件日志重放任务最终状态，快速判断上次运行是否存在失败/超时任务。

## 测试

```bash
ctest --test-dir build/debug --output-on-failure
```

当前测试覆盖：
- 有效 DAG 校验
- 环检测
- 成功路径与依赖顺序
- 失败后重试
- 失败传播导致下游 `Skipped`
- 超时语义

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

## systemd 部署示例

参考 `packaging/dag-scheduler.service`。
按实际部署路径调整 `ExecStart`、`User`、日志目录后：

```bash
sudo cp packaging/dag-scheduler.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now dag-scheduler.service
sudo systemctl status dag-scheduler.service
journalctl -u dag-scheduler.service -f
```

## 模块说明

- `WorkflowParser`: 解析 workflow 文本配置
- `DagBuilder`: 构建图、检查依赖合法性、环检测
- `ThreadPoolExecutor`: 线程池执行命令，支持超时强制终止
- `StateStore`: 任务状态机与事件记录
- `EventReplayer`: 事件日志重放与恢复摘要
- `Scheduler`: 依赖与优先级调度、失败传播、重试策略
- `Observer`: 结构化日志、报告和指标输出

## 10 天增强建议（当前代码可继续演进）

1. 事件日志重放恢复（崩溃后重启恢复）
2. 资源约束调度（CPU/IO 配额）
3. 独立 metrics HTTP 导出端点
4. 更完整的失败策略（按任务定义 fail-open/fail-close）
5. 接入 GoogleTest/Benchmark 与 CI（GitHub Actions）
