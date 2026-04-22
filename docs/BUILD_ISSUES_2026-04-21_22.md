# Build Issues Log (2026-04-21 to 2026-04-22)

## 1) `run_id` 秒级冲突导致并发运行标识重复（已修复）
- Date: 2026-04-22
- Symptom:
  - 在同一秒内并发启动两个 `run`，日志里出现相同 `run_id`（例如都为 `run-1776830754`）。
- Trigger:
  - 并发运行两个工作流实例（未显式传 `--run-id`）。
- Root cause:
  - `src/main.cpp` 的默认 `run_id` 仅使用秒级时间戳：`run-<epoch_seconds>`。
- Impact:
  - 事件日志、指标和排障时无法可靠区分并发运行实例。
- Resolution:
  - 将默认 `run_id` 生成改为：`run-<epoch_ms>-<pid>-<seq>`。
- 复测并发运行后 `run_id` 不再重复。
- Status: fixed

## 2) 任务命令中的 `#` 被错误当作注释截断（已修复）
- Date: 2026-04-22
- Symptom:
  - `cmd` 含引号内 `#` 时，运行报错：`Syntax error: Unterminated quoted string`。
- Trigger:
  - 例如 `cmd: sh -c "echo '#ok'"`。
- Root cause:
  - `src/workflow.cpp` 解析时直接按 `line.find('#')` 截断，未识别引号上下文。
- Impact:
  - 合法命令被改写，导致任务执行失败。
- Resolution:
  - 新增按引号状态解析的注释剥离逻辑，仅在非引号上下文将 `#` 视作注释起点。
- Status: fixed

## 3) CLI `--workers -1` 未被参数校验拦截（已修复）
- Date: 2026-04-22
- Symptom:
  - 运行 `--workers -1` 返回 `error: vector::reserve`。
- Trigger:
  - `dag_scheduler run --workflow ... --workers -1`
- Root cause:
  - `src/main.cpp` 使用 `std::stoul` 直接解析，负值未被前置拒绝并在后续触发异常路径。
- Impact:
  - 参数错误时错误信息不明确，且存在异常大值资源申请风险。
- Resolution:
  - 为 `--workers` 增加严格解析函数：拒绝负数、拒绝脏输入、拒绝越界值。
- Status: fixed

## 4) 数字参数接受尾随脏字符（已修复）
- Date: 2026-04-22
- Symptom:
  - `retries: 1abc` 可通过 `validate`；
  - `--max-cpu 2abc` 被当作 `2` 执行。
- Trigger:
  - 配置文件或 CLI 参数含非纯数字内容。
- Root cause:
  - `stoi/stoul` 解析后未校验“是否完整消费字符串”。
- Impact:
  - 输入校验过宽，容易隐藏配置错误。
- Resolution:
  - 工作流与 CLI 数字解析均改为严格模式，要求整字段为合法整数。
- Status: fixed

## 5) CLI `--workers` 缺少上限校验导致超大值触发内存异常（已修复）
- Date: 2026-04-22
- Symptom:
  - 运行 `--workers 1000000000` 返回 `error: std::bad_alloc`。
- Trigger:
  - `dag_scheduler run --workflow ... --workers 1000000000`
- Root cause:
  - `--workers` 仅做格式与负值校验，未限制上限；线程池初始化时直接按输入值 `reserve`。
- Impact:
  - 错误输入会触发资源冲击与不友好错误信息。
- Resolution:
  - 为 `--workers` 增加上限校验（最大 `1024`），超出时返回明确参数错误。
- Status: fixed

## 6) 2026-04-22 复测：未发现新增构建问题
- Date: 2026-04-22
- Verification run:
  - `cmake --preset debug && cmake --build --preset debug -j && ctest --test-dir build/debug --output-on-failure`
  - `cmake --preset asan && cmake --build --preset asan -j && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan --output-on-failure`
  - `cmake --preset tsan && cmake --build --preset tsan -j && ctest --test-dir build/tsan --output-on-failure`
- Result:
  - `debug/asan/tsan` 均构建成功，`dag_tests` 全部通过。
  - 未复现新的编译、链接或测试失败。
- Impact:
  - 本次仅为稳定性复核，无新增阻塞项。
- Status: closed (no new build issue)

## 7) Sanitizer 编译参数拼接错误
- Date: 2026-04-21
- Symptom:
  - `c++: error: unrecognized argument to ‘-fsanitize=’ option: ‘address -fno-omit-frame-pointer’`
- Trigger:
  - 执行 `cmake --preset asan && cmake --build --preset asan`
- Root cause:
  - `CMakeLists.txt` 中 `SAN_FLAGS` 被写成单个字符串，导致 `-fsanitize=...` 与 `-fno-omit-frame-pointer` 没有作为独立参数传递。
- Impact:
  - ASAN/TSAN 构建失败，CI sanitizer 任务不可用。
- Resolution:
  - 将 `set(SAN_FLAGS "...")` 改为列表参数：
    - `set(SAN_FLAGS -fsanitize=address -fno-omit-frame-pointer)`
    - `set(SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)`
- Status: fixed

## 8) ASAN + LeakSanitizer 在当前执行环境下不兼容
- Date: 2026-04-21
- Symptom:
  - `LeakSanitizer has encountered a fatal error`
  - `LeakSanitizer does not work under ptrace (strace, gdb, etc)`
- Trigger:
  - 执行 `ctest --test-dir build/asan --output-on-failure`
- Root cause:
  - 当前运行环境对 LSan 有限制（ptrace 相关），不是项目业务逻辑错误。
- Impact:
  - ASAN 测试在默认设置下失败，影响 CI 稳定性。
- Resolution:
  - 在测试阶段设置 `ASAN_OPTIONS=detect_leaks=0`。
  - 已同步到 CI workflow 的 asan matrix 配置。
- Status: mitigated (environment-specific)

## 9) 工具链缺失：`rg` 不可用
- Date: 2026-04-21
- Symptom:
  - `/bin/bash: line 1: rg: command not found`
- Trigger:
  - 初次使用 `rg --files` 搜索代码文件。
- Root cause:
  - 环境未安装 ripgrep。
- Impact:
  - 仅影响本地检索效率，不影响构建和测试结果。
- Resolution:
  - 临时改用 `find` / `grep` / `sed`。
- Status: open (non-blocking)

## 10) 受限沙箱导致 Git 写操作失败（环境问题）
- Date: 2026-04-21
- Symptom:
  - `fatal: Unable to create .../.git/index.lock: Read-only file system`
- Trigger:
  - 首次执行 `git add`。
- Root cause:
  - 运行环境沙箱默认限制写入权限。
- Impact:
  - 无法直接 stage/commit，需要提升执行权限。
- Resolution:
  - 使用提升权限的命令执行 Git 写操作。
- Status: mitigated (environment-specific)

## 11) `--resume` 启动阶段重复发出 `ready` 事件（已修复）
- Date: 2026-04-22
- Symptom:
  - `--resume` 时部分任务出现重复 `ready` 事件（同一任务被重复入队）。
- Trigger:
  - 上游任务被恢复为 `Succeeded` 后，下游任务先在恢复逻辑中入队，随后初始化扫描再次入队。
- Root cause:
  - 调度器启动阶段对 `deg==0` 且非终态任务统一 `enqueue_ready`，未排除已处于 `Ready` 的任务。
- Impact:
  - 事件日志重复、就绪队列重复项、排障与队列等待统计受干扰。
- Resolution:
  - 启动扫描改为仅对 `Pending` 状态任务执行初次 `enqueue_ready`。
- Status: fixed

## 12) 失败原因指标将 `exit 130` 误归类为 `signal`（已修复）
- Date: 2026-04-22
- Symptom:
  - 任务 `exit_code=130` 且消息为 `non-zero exit` 时，`dag_failures_total{reason="signal"}` 增加。
- Trigger:
  - 执行 `sh -c 'exit 130'`。
- Root cause:
  - 失败原因分桶按 `exit_code >= 128` 判断 `signal`，与真实执行语义不一致。
- Impact:
  - Prometheus 失败原因统计失真，影响故障分析。
- Resolution:
  - 改为按执行结果消息分类：`terminated by signal` 记为 `signal`，其余失败记为 `non_zero_exit`。
- Status: fixed

## 13) 任务超时时后台子进程未被回收（已修复）
- Date: 2026-04-22
- Symptom:
  - 任务因超时结束后，`cmd` 启动的后台子进程（例如 `sleep 30 & ...`）仍存活。
- Trigger:
  - `timeout_ms` 触发后，命令为 `sh -c 'sleep 30 & ...; wait'`。
- Root cause:
  - 超时分支仅向父进程 PID 发信号，未覆盖整个子进程树；且 `waitpid` 对 `EINTR` 处理不完整。
- Impact:
  - 产生残留进程，可能造成资源泄漏和后续任务干扰。
- Resolution:
  - 每个任务进程建立独立 process group，超时时按 process group 发送 `SIGTERM/SIGKILL`。
  - 补充 `waitpid` 的 `EINTR` 重试与失败分支清理。
- Status: fixed

## 14) `fail_fast` 在 `--resume` 已失败状态下未生效（已修复）
- Date: 2026-04-22
- Symptom:
  - `fail_fast=true` 且恢复状态中已有失败任务时，独立任务仍会被继续执行。
- Trigger:
  - `--resume` 恢复 `A=Failed`，工作流中独立任务 `B` 仍被调度运行。
- Root cause:
  - 恢复状态应用后，调度器未立即执行 `fail_fast` 跳过逻辑。
- Impact:
  - 与 `fail_fast` 语义不一致，恢复执行可能产生不期望副作用。
- Resolution:
  - 恢复状态应用后，若已有 `Failed/TimedOut` 且 `fail_fast=true`，立即将未终态任务标记为 `skipped`。
- Status: fixed

## Summary
- 构建相关核心问题：2 个（Sanitizer 参数拼接错误、ASAN LSan 环境兼容性）。
- 环境与效率问题：2 个（`rg` 缺失、Git 沙箱写权限限制）。
- 运行稳定性与输入校验问题：9 个（默认 `run_id` 秒级冲突、`cmd` 中 `#` 截断、`--workers` 负值校验缺失、数字尾随脏字符未拦截、`--workers` 超大值上限缺失、`--resume` 重复 `ready` 事件、失败原因指标误分类、超时后子进程残留、`--resume` 下 `fail_fast` 语义未生效），均已修复。
- 2026-04-22 复测结论：无新增构建问题，`debug/asan/tsan` 流程稳定通过（ASAN 仍使用 `ASAN_OPTIONS=detect_leaks=0`）。
