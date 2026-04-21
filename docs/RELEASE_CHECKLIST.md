# 发布清单 / Release Checklist

## 预发布门禁 / Pre-release Gate
- [x] `cmake --preset debug && cmake --build --preset debug`
- [x] `ctest --test-dir build/debug --output-on-failure`
- [x] `cmake --preset asan && cmake --build --preset asan && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan --output-on-failure`
- [x] `cmake --preset tsan && cmake --build --preset tsan && ctest --test-dir build/tsan --output-on-failure`
- [ ] GitHub Actions CI 在 `main` 绿灯 / GitHub Actions CI is green on `main`

## 文档与打包门禁 / Docs and Packaging Gate
- [x] `README.md` 已覆盖全部 CLI 选项与 workflow 字段 / reflects all CLI options and workflow keys
- [ ] `docs/DEPLOYMENT.md` 已在干净 Debian 主机验证 / validated on a clean Debian host
- [x] `packaging/dag-scheduler.service` 路径和用户组正确 / uses correct paths and user/group
- [x] `CHANGELOG.md` 已更新 / updated for this release

## 版本与打标签门禁 / Versioning and Tagging Gate
- [x] 已升版本（`CMakeLists.txt` / release notes） / version bumped
- [x] 已创建 annotated tag（示例：`v1.0.0`）- 本地 tag `v0.2.0` / annotated tag created locally
- [x] 发布说明包含关键变更和已知限制 / release notes include key changes and known limitations

## 发布后门禁 / Post-release Gate
- [x] 本地产物已生成（`dist/my-dag-scheduler-0.2.0-linux-x86_64.tar.gz` + `.sha256`） / local artifact built
- [x] 本地 smoke 已通过（`build/release/smoke_report.json`、`smoke_metrics.prom`、`smoke_events.jsonl`、`smoke_run.log`） / local smoke run passed
- [ ] 二进制/包产物已挂到 Release / binary/package artifact attached to release
- [ ] 目标主机 smoke 已执行 / smoke run performed on target host
- [x] 已有上个稳定版回滚方案（`docs/ROLLBACK.md`） / rollback plan documented for previous stable
