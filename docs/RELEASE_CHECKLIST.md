# Release Checklist

## Pre-release Gate
- [x] `cmake --preset debug && cmake --build --preset debug`
- [x] `ctest --test-dir build/debug --output-on-failure`
- [x] `cmake --preset asan && cmake --build --preset asan && ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build/asan --output-on-failure`
- [x] `cmake --preset tsan && cmake --build --preset tsan && ctest --test-dir build/tsan --output-on-failure`
- [ ] GitHub Actions CI is green on `main`

## Docs and Packaging Gate
- [x] `README.md` reflects all CLI options and workflow keys
- [ ] `docs/DEPLOYMENT.md` validated on a clean Debian host
- [x] `packaging/dag-scheduler.service` uses correct paths and user/group
- [x] `CHANGELOG.md` updated for this release

## Versioning and Tagging Gate
- [x] Version bumped (`CMakeLists.txt` / release notes)
- [ ] Annotated tag created (example: `v1.0.0`)
- [x] Release notes include key changes and known limitations

## Post-release Gate
- [x] Local artifact built (`dist/my-dag-scheduler-0.2.0-linux-x86_64.tar.gz` + `.sha256`)
- [x] Local smoke run passed (`build/release/smoke_report.json`, `smoke_metrics.prom`, `smoke_events.jsonl`, `smoke_run.log`)
- [ ] Binary/package artifact attached to release
- [ ] Smoke run performed on target host
- [x] Rollback plan documented for previous stable version (`docs/ROLLBACK.md`)
