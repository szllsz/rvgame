# 精确设备的手柄优先兼容服务

对应原 AX7020/PYNQ 板上的旧 `05e3:0608` hub、G102、VADER4 有线手柄和 P008 触屏。公开版本须显式配置 `RVGAME_G102_SERIAL`，默认不操作任何鼠标。匹配序列号及两份 HID 描述符后，停用未使用的附加键盘接口；手柄重枚举时暂时解绑鼠标主接口，确认手柄有效输入后恢复鼠标。不重置USB主机或PL。

本板原服务已安装启用，普通及约1秒手柄重插曾获用户确认。11月3日另有运行中三输入失效、重启恢复的报告，原因未定。公开版仅增加序列号环境配置，未在板上替换原服务；冷启动、长休眠及其他型号未完整验收。`run.py` 不自动安装此系统服务。

在另一个已核准环境安装前，先检查源/服务字节及设备身份，确保没有其他USB恢复试验。以下路径是本板部署约定：

```sh
sudo install -D -m 0644 usb_priority_guard.py /usr/local/lib/rvgame-usb-priority-20261102/usb_priority_guard.py
sudo install -m 0644 rvgame-usb-priority.service /etc/systemd/system/rvgame-usb-priority.service
sudo systemd-analyze verify /etc/systemd/system/rvgame-usb-priority.service
sudo systemctl daemon-reload
```

启用前用 `sudo systemctl edit rvgame-usb-priority.service` 添加下列配置，将占位值换成核准设备的实际序列号；不要直接使用占位值：

```ini
[Service]
Environment=RVGAME_G102_SERIAL=YOUR_VERIFIED_SERIAL
```

然后执行 `sudo systemctl enable --now rvgame-usb-priority.service`。序列号匹配只是精确设备试验边界，不代表任意同型号硬件都已验证。

运行状态在 `/run/rvgame-usb-priority/state.json`；日志用 `journalctl -u rvgame-usb-priority.service` 查看。服务持有 `/run/lock/p008-usb-host-recovery.lock`，其他USB主机恢复操作前必须正常停服务并确认鼠标两接口恢复。它会改变USB驱动绑定，不能标为只读观察器。

回退用 `sudo systemctl disable --now rvgame-usb-priority.service`，随后核对 `stopped_restored` 及主/附加接口的 usbhid 绑定。保留文件和失败收据，不需批量删除。
