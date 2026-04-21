# 回滚方案（回退到上个稳定版本） / Rollback Plan (to Previous Stable)

## 范围 / Scope
该方案用于在 Debian systemd 主机上，将 `dag-scheduler.service` 从当前版本回退到已知稳定版本。  
This plan rolls back `dag-scheduler.service` from current release to a known stable release on Debian systemd hosts.

## 必要输入 / Inputs Required
- 目标回滚版本（例如 `0.1.0`） / target rollback version
- 对应产物（例如 `my-dag-scheduler-0.1.0-linux-x86_64.tar.gz`） / matching artifact
- 现有运行路径 / existing runtime paths:
- `/opt/my-dag-scheduler`
- `/var/lib/my-dag-scheduler`
- `/var/log/my-dag-scheduler`

## 1) 停服务并备份当前状态 / Stop Service and Snapshot Current State
```bash
sudo systemctl stop dag-scheduler.service
sudo mkdir -p /var/backups/my-dag-scheduler
sudo tar -C / -czf /var/backups/my-dag-scheduler/pre-rollback-$(date +%Y%m%d-%H%M%S).tar.gz \
  opt/my-dag-scheduler \
  var/lib/my-dag-scheduler \
  var/log/my-dag-scheduler \
  etc/systemd/system/dag-scheduler.service
```

## 2) 安装上一稳定版文件 / Install Previous Stable Binary and Files
```bash
tar -xzf my-dag-scheduler-0.1.0-linux-x86_64.tar.gz
sudo install -m 0755 my-dag-scheduler-0.1.0/bin/dag_scheduler /opt/my-dag-scheduler/bin/dag_scheduler
sudo install -m 0644 my-dag-scheduler-0.1.0/workflows/sample.workflow /opt/my-dag-scheduler/workflows/sample.workflow
sudo install -m 0644 my-dag-scheduler-0.1.0/packaging/dag-scheduler.service /etc/systemd/system/dag-scheduler.service
```

## 3) 重载并启动服务 / Reload and Start Service
```bash
sudo systemctl daemon-reload
sudo systemctl start dag-scheduler.service
sudo systemctl status dag-scheduler.service --no-pager
```

## 4) 回滚结果验证 / Validate Rollback Result
```bash
/opt/my-dag-scheduler/bin/dag_scheduler --help | head -n 5
ls -l /var/lib/my-dag-scheduler/report.json /var/lib/my-dag-scheduler/metrics.prom
sudo journalctl -u dag-scheduler.service -n 100 --no-pager
```

## 5) 回滚失败处理 / If Rollback Failed
- 恢复步骤 1 生成的备份归档。 / restore from backup tar created in step 1.
- 服务保持停止，先确认状态与二进制一致性。 / keep service stopped until state/binary consistency is confirmed.
- 重新执行验证后再开启自动拉起。 / re-run validation before enabling auto-restart.
