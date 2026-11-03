# 精确设备的输入预留与 USB 停滞恢复

适用于已核准的 AX7020/PYNQ、05e3:0608 单 TT 集线器、G102、有线 VADER4 与 P008 组合。程序不访问 PL/MMIO，也不改变内核或轮询间隔。其操作会改变鼠标接口绑定，显式启用恢复选项后还可能重绑 ci_hdrc.0，不能作为只读工具使用。

手柄重新枚举或同地址出现 stale／传输错误时，先释放鼠标主接口，等手柄有效解码后恢复。USB IRQ 至少 5 秒无进展且输入异常时，才检查 hub 与鼠标的标准设备描述符；两个请求均超时且完整拓扑仍匹配，才恢复 USB 主机。安静的设备只要控制请求正常，就不触发主机恢复。最多每 5 分钟恢复两次。

启动时等待应用能够读取 USB，再计算独立的 12 秒预留窗口；初始等待上限 90 秒。

## 安装与运行

离线自检：`python test_guard.py --output <新目录>`。自检通过模拟系统接口验证恢复边界，不访问硬件。

公开服务通过 `RVGAME_G102_SERIAL` 显式匹配核准鼠标的序列号；未配置时不改变鼠标绑定。

先按 runtime/rollback.md 让现有应用正常退休，检查程序／显示缓冲释放和 HP 恢复，再停止旧 USB helper。不能在手柄读取仍运行时直接恢复附加鼠标接口。只有确认旧 helper 已退出、清理完成，才继续以下步骤；清理失败保留现场，不能叠加新恢复流程。

```sh
sudo install -D -m 0644 usb_priority_guard.py /usr/local/lib/rvgame-usb-priority-20261103-b/usb_priority_guard.py
sudo install -m 0644 rvgame-usb-priority.service /etc/systemd/system/rvgame-usb-priority.service
sudo systemd-analyze verify /etc/systemd/system/rvgame-usb-priority.service
sudo systemctl edit rvgame-usb-priority.service
```

服务覆盖配置：

```ini
[Service]
Environment=RVGAME_G102_SERIAL=YOUR_VERIFIED_SERIAL
```

替换占位值并保存后，运行 `sudo systemctl daemon-reload` 和 `sudo systemctl enable --now rvgame-usb-priority.service`，再启动应用。保留既有单一显示 owner；不要叠加第二个运行入口。

状态文件为 `/run/rvgame-usb-priority/state.json`，日志通过 `journalctl -u rvgame-usb-priority.service` 查看。程序持有 `/run/lock/p008-usb-host-recovery.lock`。手动停止／回退同样先退休应用，再停 helper 并确认 `stopped_restored`；系统关机时 `Before=rvgame-unified-boot.service` 提供逆序停止关系。
