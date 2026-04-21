# Rollback Plan (to Previous Stable)

## Scope
This plan rolls back `dag-scheduler.service` from current release to a known stable release on Debian systemd hosts.

## Inputs Required
- Target rollback version (example: `0.1.0`)
- Matching artifact (example: `my-dag-scheduler-0.1.0-linux-x86_64.tar.gz`)
- Existing runtime paths:
  - `/opt/my-dag-scheduler`
  - `/var/lib/my-dag-scheduler`
  - `/var/log/my-dag-scheduler`

## 1) Stop service and snapshot current state
```bash
sudo systemctl stop dag-scheduler.service
sudo mkdir -p /var/backups/my-dag-scheduler
sudo tar -C / -czf /var/backups/my-dag-scheduler/pre-rollback-$(date +%Y%m%d-%H%M%S).tar.gz \
  opt/my-dag-scheduler \
  var/lib/my-dag-scheduler \
  var/log/my-dag-scheduler \
  etc/systemd/system/dag-scheduler.service
```

## 2) Install previous stable binary and files
```bash
tar -xzf my-dag-scheduler-0.1.0-linux-x86_64.tar.gz
sudo install -m 0755 my-dag-scheduler-0.1.0/bin/dag_scheduler /opt/my-dag-scheduler/bin/dag_scheduler
sudo install -m 0644 my-dag-scheduler-0.1.0/workflows/sample.workflow /opt/my-dag-scheduler/workflows/sample.workflow
sudo install -m 0644 my-dag-scheduler-0.1.0/packaging/dag-scheduler.service /etc/systemd/system/dag-scheduler.service
```

## 3) Reload and start service
```bash
sudo systemctl daemon-reload
sudo systemctl start dag-scheduler.service
sudo systemctl status dag-scheduler.service --no-pager
```

## 4) Validate rollback result
```bash
/opt/my-dag-scheduler/bin/dag_scheduler --help | head -n 5
ls -l /var/lib/my-dag-scheduler/report.json /var/lib/my-dag-scheduler/metrics.prom
sudo journalctl -u dag-scheduler.service -n 100 --no-pager
```

## 5) If rollback failed
- Restore from backup tar created in step 1.
- Keep service stopped until state and binary consistency is confirmed.
- Re-run validation commands before enabling automatic restart.
