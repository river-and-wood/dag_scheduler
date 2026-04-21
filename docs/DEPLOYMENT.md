# Deployment Guide (Debian)

## Scope
This runbook deploys `dag_scheduler` on a fresh Debian host using systemd.

## 1) Create runtime user and directories
```bash
sudo useradd --system --home /opt/my-dag-scheduler --shell /usr/sbin/nologin dag || true
sudo mkdir -p /opt/my-dag-scheduler/bin /opt/my-dag-scheduler/workflows
sudo mkdir -p /var/lib/my-dag-scheduler /var/log/my-dag-scheduler
sudo chown -R dag:dag /opt/my-dag-scheduler /var/lib/my-dag-scheduler /var/log/my-dag-scheduler
```

## 2) Build and install binary
```bash
cmake --preset release
cmake --build --preset release
sudo install -m 0755 build/release/dag_scheduler /opt/my-dag-scheduler/bin/dag_scheduler
```

## 3) Install workflow file
```bash
sudo install -m 0644 examples/sample.workflow /opt/my-dag-scheduler/workflows/sample.workflow
sudo chown dag:dag /opt/my-dag-scheduler/workflows/sample.workflow
```

## 4) Install and enable service
```bash
sudo install -m 0644 packaging/dag-scheduler.service /etc/systemd/system/dag-scheduler.service
sudo systemctl daemon-reload
sudo systemctl enable --now dag-scheduler.service
```

## 5) Verify runtime
```bash
sudo systemctl status dag-scheduler.service
sudo journalctl -u dag-scheduler.service -n 100 --no-pager
ls -l /var/lib/my-dag-scheduler/report.json /var/lib/my-dag-scheduler/metrics.prom
ls -l /var/log/my-dag-scheduler/events.jsonl /var/log/my-dag-scheduler/run.log
```

## 6) Replay from event log
```bash
/opt/my-dag-scheduler/bin/dag_scheduler replay --events /var/log/my-dag-scheduler/events.jsonl
```

## Troubleshooting
- If service fails at startup, check command and permissions in `/etc/systemd/system/dag-scheduler.service`.
- If no output files are generated, confirm `ReadWritePaths` and directory ownership.
- If you restart after interruption, `--resume` will reuse prior `events.jsonl` states.
