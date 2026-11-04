# P008 USB 持续轮询兼容服务

本服务为报告描述符 SHA256 匹配的 `1a86:e5e3` 触屏打开 Linux evdev 节点并持续读取事件。它不抓取手柄、不注入按键、不写 PL/MMIO，也不重置 USB。触摸菜单由应用输入层处理。

适用环境为本包说明的 AX7020/PYNQ、P008 与 `05e3:0608` 集线器组合。触屏 USB 提供输入与供电，HDMI 接开发板。`install.py` 核对设备及服务文件后安装：

```sh
sudo python3 install.py --install
sudo systemctl status rvgame-p008-poll.service
```

安装位置为 `/usr/local/lib/rvgame-p008-poll-20261102-a/keeper.py` 和 `/etc/systemd/system/rvgame-p008-poll.service`；每次启动在 `/run/rvgame-p008-poll/` 单独记录日志。默认有界采样为 120 秒，systemd 常驻服务使用 `--seconds 0`。

上游 [Linux v6.6 usbhid](https://github.com/torvalds/linux/blob/v6.6/drivers/hid/usbhid/hid-core.c) 在普通输入关闭时停止轮询。本服务通过用户态 evdev 消费者保持持续读取，不修改内核或启动参数。

回退命令为 `sudo systemctl disable --now rvgame-p008-poll.service`，服务正常关闭输入句柄。停止持续读取会恢复设备原有轮询行为；其他设备或系统环境应使用匹配的驱动配置。
