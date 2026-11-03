# 独立运行目录

本目录含八个 RV32 程序、已经上板验证的 native800 bit/HWH、素材/PCM、PS Python 源码与统一入口。正常运行从菜单开始，未附自动按键驱动。它面向已有合适 AX7020 PYNQ 环境的开发板，不是完整 SD 卡镜像；不同内核、CMA 或板卡需要重新资格验证。

环境：ALINX AX7020 / XC7Z020CLG400-2、Linux/PYNQ 3.1.2、Python 3、NumPy、板端共享库 libusb-1.0、可用 CMA 内存，以及匹配的 HDMI 和音频硬件。RV32 编译器和 Vivado 只在重建时需要，板端运行不需要。主菜单可接收鼠标evdev和已核P008触屏；P008适配依赖本板已启用的HID debugfs报告接口，不等于任意Linux镜像均兼容。原有有线/2.4G手柄路径保留。

1. 完整复制 `runtime` 目录到板上一个新路径，不覆盖原稳定目录。
2. 在任意有 Python 3 的主机执行 `python run.py`，只核对 SHA256 与八程序配置，不访问硬件。
3. 在板上确认原有 rvgame owner/服务已正常退休，且其日志明确释放程序、显示缓冲并恢复 HP。若仍有 owner，入口将拒绝启动，不会替你强杀。
4. 在板上该目录执行 `sudo -E python3 run.py --start`。使用原 PYNQ 环境的解释器；如果 sudo 下找不到 PYNQ，应指定原环境解释器完整路径，不能盲目安装另一个版本。
5. P008需要持续evdev轮询以避免已复现的停止读取后重枚举。已有同版服务可继续使用；另见 `usb_touch/readme.md`，校验后可运行 `sudo python3 usb_touch/install.py --install`。该独立辅助服务不写PL/MMIO、不重置USB。
6. 菜单控制见 `controls.md`。触屏单击选中、双击卡片进入；鼠标左键单击进入，右键或触屏底部SELECT区域返回。虚拟方向/OK条已移除，游戏操作仍用手柄。默认音量5，星际战斗音乐从原曲第5秒开始，首页音乐不变。星际LT/RT选择三挡弹幕，鸡蛋展示LT/RT选择三波；鸡蛋同波内部保留10秒场景循环，不自动跳到下一波。停止和恢复见 `rollback.md`，入口会核对本次正常退休与cleanup。

正式安装与本目录的可执行程序/bit/HWH/PCM/PS字节身份见 `../verification/runtime_packaging.json`。主菜单回放、进入/返回回放与正常安装启动分别记录。独立入口收据见`../verification/standalone_board.json`；只有其release_sha256与本目录清单一致，且`../verification/standalone_status.json`明确通过，才属于当前版入口板测。历史收据保留原版本身份。所有已有启动证据针对原板环境，未在全新SD卡或第二块板上安装，不替代实体操作验收。

旧集线器的历史ENOSPC/重枚举停滞仍保留原记录。当前精确G102/VADER4/P008组合由 `usb_priority/` 兼容服务调度，已通过普通及约1秒重插人工确认；更广设备组合、长休眠及冷启动未验收。服务单独安装，run.py不会自动修改系统。它不写PL/MMIO，但会改变精确鼠标接口绑定；其他USB主机试验前须正常停服务，详见该目录README。早期8ms/异步候选未采用。
