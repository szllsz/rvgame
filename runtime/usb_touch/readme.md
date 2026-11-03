# P008 USB 持续轮询兼容服务

本服务只为报告描述符SHA256匹配的`1a86:e5e3`触屏打开Linux evdev节点并排空输入事件。它不抓取手柄、不注入按键、不写PL/MMIO，不解绑USB驱动、不自动重置USB，也不实现触摸菜单。

本轮固定接线为板卡→旧`05e3:0608`集线器，手柄在USB路径port2、触屏在port4；额外安卓口经安卓线由电脑USB供电，HDMI仍接板卡。这是软件枚举路径，不保证外壳上有相同端口标号。电压/电流未测。

未打开触摸读取、用户实际点按的120.03秒观察中出现9次触屏掉线；持续读取约184秒未出现掉线；轻量evdev候选120.19秒收到1557事件、一次打开、零断开，部署后的两分钟观察也无新USB错误，用户确认操作与静置后手柄正常。原始证据见`reports/hub_touch_gamepad_20261102_a/polling_compatibility_summary.json`。184秒窗口因用户将原计划五分钟缩短为两分钟而主动结束，停止请求处理时已超过两分钟，原非零退出/KeyboardInterrupt收据保留，不能把它改成完整五分钟或设备故障。

上游[Linux v6.6 usbhid实现](https://github.com/torvalds/linux/blob/v6.6/drivers/hid/usbhid/hid-core.c)中，普通输入关闭会停止输入轮询；`HID_QUIRK_ALWAYS_POLL`提供持续轮询机制。此处使用普通用户态evdev消费者达到持续轮询效果，未修改内核或启动参数。对照支持本设备组合的兼容处理，不足以证明全部历史整条总线停滞的唯一根因，也不代替断电启动及长期验收。

已安装文件：

- `/usr/local/lib/rvgame-p008-poll-20261102-a/keeper.py`
- `/etc/systemd/system/rvgame-p008-poll.service`
- 每次服务启动单独记录在`/run/rvgame-p008-poll/poll_*.json`，不覆盖前次同boot记录。

查看状态：`sudo systemctl status rvgame-p008-poll.service`。

回退：`sudo systemctl disable --now rvgame-p008-poll.service`。程序会正常关闭输入句柄，不重置USB；版本化文件可保留。此操作恢复原来的无人读取状态，仍连接并操作触屏时可能再次触发已复现现象。

源程序默认有界测试120秒；`--seconds 0`仅供systemd常驻服务使用。每个测试窗口按用户最新要求两分钟，发现问题提前记录，不再安排默认五分钟观察。
