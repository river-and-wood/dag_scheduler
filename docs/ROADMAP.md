# DAG Scheduler 路线图 / DAG Scheduler Roadmap

## 项目目标 / Project Goal
构建一个面向 Debian、可用于生产的本地 DAG 调度器，具备可靠执行、回放恢复、可观测性、CI 质量门禁和可复现发布流程。  
Build a production-ready local DAG scheduler for Debian with reliable execution, replay/recovery, observability, CI quality gates, and reproducible releases.

## 完成定义（DoD） / Definition of Done
- `validate`、`run`、`replay` 在正常与失败场景均稳定。  
  Core commands are stable on normal and failure cases.
- 主干 CI 全绿（构建、测试、sanitizer）。  
  CI is green on main branch.
- 运维文档完整（日志/事件/指标/systemd 部署）。  
  Ops docs are complete.
- 发布流程可重复执行（版本、变更记录、清单）。  
  Release process is repeatable.

## 里程碑 / Milestones

### M1: 基线与规划 / Baseline and Planning
- Scope: 冻结当前基线，定义交付顺序和验收标准。 / freeze baseline, define order and acceptance.
- Deliverables:
- `docs/ROADMAP.md`
- `docs/EXECUTION_PLAN.md`
- Acceptance:
- 每个步骤都有明确产出和可测验证。 / explicit output and measurable verification
- 执行顺序固定且有优先级。 / execution order is fixed and prioritized

### M2: CI 与质量护栏 / CI and Quality Guardrails
- Scope: 建立 push/PR 自动质量检查。 / set automated quality checks for push/PR.
- Deliverables:
- GitHub Actions: Debug build + CTest
- sanitizer jobs (ASAN/TSAN)
- Acceptance:
- CI 自动触发。 / CI runs automatically
- 失败检查可阻止合并（按策略）。 / failing checks block merge by policy

### M3: 测试覆盖加固 / Test Coverage Hardening
- Scope: 补齐 parser/scheduler 失败路径覆盖。 / close coverage gaps in failure paths.
- Deliverables:
- 重复 task id、缺失依赖、非法 key、fail_fast 等测试 / tests for duplicate id, missing dep, invalid keys, fail_fast
- timeout/retry 回归测试 / timeout/retry regression tests
- Acceptance:
- 关键边界与失败路径有自动化测试。 / critical edge/failure paths covered
- 每个修复问题至少有一条回归测试。 / at least one regression test per fixed bug

### M4: 回放鲁棒性与恢复 / Replay Robustness and Resume
- Scope: 强化回放容错并支持恢复执行。 / improve replay tolerance and add resume.
- Deliverables:
- 兼容部分损坏 JSONL 行 / robust parsing for partially corrupted JSONL lines
- `run --resume --events <path>`
- Acceptance:
- 坏行不导致回放崩溃。 / damaged lines do not crash replay
- 中断后可基于事件日志继续。 / interrupted run can continue from prior events

### M5: 调度与可观测性增强 / Scheduling and Observability Enhancements
- Scope: 增强资源感知与运行可见性。 / improve resource-awareness and runtime visibility.
- Deliverables:
- 任务级资源约束（如 cpu/io） / task-level resource constraints
- 更丰富指标（排队时延、重试率、失败原因桶） / richer metrics
- 可选 metrics HTTP 端点 / optional metrics HTTP endpoint
- Acceptance:
- 高并发下遵守资源上限并保持稳定。 / stable under configured limits
- 指标可被 Prometheus/Grafana 消费。 / metrics consumable by Prometheus/Grafana

### M6: 打包、运维与发布 / Packaging, Ops, and Release
- Scope: 达到运维就绪并可版本发布。 / operational readiness and versioned release.
- Deliverables:
- 完整部署文档与 systemd 模板 / finalized deployment docs + systemd template
- `CHANGELOG.md` 与语义化版本流程 / changelog + semver process
- 发布清单与 tag 流程 / release checklist and tagging workflow
- Acceptance:
- 干净 Debian 主机可按文档端到端部署。 / clean Debian host deployment works end-to-end
- 基于仓库状态可复现 tagged release。 / tagged release is reproducible

## 优先级规则 / Prioritization Rules
- 先可靠性，后功能。 / reliability before features
- 先测试，后重构。 / tests before refactor
- 先可观测性，后性能优化。 / observability before performance optimization
- CI 与部署文档完成后再发布。 / release only after CI and docs are complete

## 建议排期（2 周） / Proposed 2-Week Schedule
- Week 1: M1, M2, M3, M4
- Week 2: M5, M6

## 风险登记 / Risk Register
- 风险：executor/scheduler 并发隐藏竞态。 / hidden race conditions in concurrency.
- 缓解：CI 引入 TSAN + 定向压力测试。 / TSAN in CI + targeted stress tests.
- 风险：手写 JSON 字符串解析带来 replay 脆弱性。 / replay brittleness due to manual JSON string parsing.
- 缓解：防御式解析与坏行测试。 / defensive parsing and malformed-line tests.
- 风险：文档与生产部署偏离。 / production drift between docs and deploy reality.
- 缓解：部署 smoke 清单纳入发布门禁。 / deployment smoke checklist as release gate.
