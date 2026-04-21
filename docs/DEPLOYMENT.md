# 部署手册（Debian） / Deployment Guide (Debian)

## 适用范围 / Scope
本文档用于在全新 Debian 主机上通过 systemd 部署 `dag_scheduler`。  
This runbook deploys `dag_scheduler` on a fresh Debian host using systemd.

## 1) 创建运行用户与目录 / Create Runtime User and Directories
```bash
sudo useradd --system --home /opt/my-dag-scheduler --shell /usr/sbin/nologin dag || true
sudo mkdir -p /opt/my-dag-scheduler/bin /opt/my-dag-scheduler/workflows
sudo mkdir -p /var/lib/my-dag-scheduler /var/log/my-dag-scheduler
sudo chown -R dag:dag /opt/my-dag-scheduler /var/lib/my-dag-scheduler /var/log/my-dag-scheduler
```

## 2) 构建并安装二进制 / Build and Install Binary
```bash
cmake --preset release
cmake --build --preset release
sudo install -m 0755 build/release/dag_scheduler /opt/my-dag-scheduler/bin/dag_scheduler
```

## 3) 安装工作流文件 / Install Workflow File
```bash
sudo install -m 0644 examples/sample.workflow /opt/my-dag-scheduler/workflows/sample.workflow
sudo chown dag:dag /opt/my-dag-scheduler/workflows/sample.workflow
```

## 4) 安装并启用服务 / Install and Enable Service
```bash
sudo install -m 0644 packaging/dag-scheduler.service /etc/systemd/system/dag-scheduler.service
sudo systemctl daemon-reload
sudo systemctl enable --now dag-scheduler.service
```

## 5) 运行验证 / Verify Runtime
```bash
sudo systemctl status dag-scheduler.service
sudo journalctl -u dag-scheduler.service -n 100 --no-pager
ls -l /var/lib/my-dag-scheduler/report.json /var/lib/my-dag-scheduler/metrics.prom
ls -l /var/log/my-dag-scheduler/events.jsonl /var/log/my-dag-scheduler/run.log
```

## 6) 从事件日志重放 / Replay from Event Log
```bash
/opt/my-dag-scheduler/bin/dag_scheduler replay --events /var/log/my-dag-scheduler/events.jsonl
```

## 故障排查 / Troubleshooting
- 若服务启动失败，请检查 `/etc/systemd/system/dag-scheduler.service` 中的命令与权限。  
  If startup fails, check command and permissions in `/etc/systemd/system/dag-scheduler.service`.
- 若没有输出文件，请检查 `ReadWritePaths` 和目录属主。  
  If no outputs are generated, verify `ReadWritePaths` and directory ownership.
- 中断后重启时，`--resume` 会复用历史 `events.jsonl` 状态。  
  After interruption, `--resume` reuses prior states from `events.jsonl`.
