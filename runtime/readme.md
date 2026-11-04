# 独立运行目录

本目录含八个 RV32 程序、已经上板验证的 native800 bit/HWH、素材/PCM、PS Python 源码与统一入口。正常运行从菜单开始，未附自动按键驱动。它面向已有合适 AX7020 PYNQ 环境的开发板，不是完整 SD 卡镜像；不同内核、CMA 或板卡需要重新资格验证。

环境：ALINX AX7020 / XC7Z020CLG400-2、Linux/PYNQ 3.1.2、Python 3、NumPy、板端共享库 libusb-1.0、可用 CMA 内存，以及匹配的 HDMI 和音频硬件。RV32 编译器和 Vivado 只在重建时需要，板端运行不需要。主菜单可接收鼠标evdev和已核P008触屏；P008适配依赖本板已启用的HID debugfs报告接口，不等于任意Linux镜像均兼容。原有有线/2.4G手柄路径保留。

1. 完整复制 `runtime` 目录到板上一个新路径，不覆盖原稳定目录。
2. 在任意有 Python 3 的主机执行 `python run.py`，只核对 SHA256 与八程序配置，不访问硬件。
3. 在板上确认原有 rvgame owner/服务已正常退休，且其日志明确释放程序、显示缓冲并恢复 HP。若仍有 owner，入口将拒绝启动，不会替你强杀。
4. 在板上该目录执行 `sudo -E python3 run.py --start`。使用原 PYNQ 环境的解释器；如果 sudo 下找不到 PYNQ，应指定原环境解释器完整路径，不能盲目安装另一个版本。
5. P008需要持续evdev轮询以避免已复现的停止读取后重枚举。已有同版服务可继续使用；另见 `usb_touch/readme.md`，校验后可运行 `sudo python3 usb_touch/install.py --install`。该独立辅助服务不写PL/MMIO、不重置USB。
6. 菜单控制见 `controls.md`。触屏单击选中、双击卡片进入；鼠标左键单击进入，右键或触屏底部SELECT区域返回。虚拟方向/OK条已移除，游戏操作仍用手柄。默认音量5。星际LT/RT选择三挡弹幕，鸡蛋展示LT/RT选择三波；鸡蛋同波内部保留10秒场景循环，不自动跳到下一波。停止和恢复见 `rollback.md`，入口会核对本次正常退休与cleanup。

文件身份见本目录 `release_manifest.json`。软件重建和离线测量复核结果见 `../verification/reproduction.json`。当前包面向上述 PYNQ 环境，完整依赖随包提供；安装前仍须配置目标系统的 PYNQ、CMA、显示与 USB 接口。

G102/VADER4/P008 组合可安装 `usb_priority/` 中的辅助服务。它不写 PL/MMIO，但会改变匹配鼠标的接口绑定；默认启动入口不会自动安装系统服务。安装或替换辅助服务时，先按 `rollback.md` 正常退出应用，再停止辅助服务。

本公开包采用六卡片菜单，运行层包含按需加载、PCM 批量写入和 USB 查询退避。菜单源码、二进制与点击区域保持同一公开配置。PCM 写入库附有 C 源码，在目标 ARM Linux 上进入 `runtime/package` 后可用以下命令重建：

```sh
gcc -shared -fPIC -O3 -o libpcm_fifo_write.so pcm_fifo_write.c
```

随附 `.so` 用于原 ARM/PYNQ 环境；其他环境重新编译后需在目标板验证。
