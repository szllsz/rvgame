# 从本目录复现

**当前公开版：`native800_final_visual_a` 应用与 20261103-b USB 恢复服务。** 主菜单触屏双击、鼠标单击进入；音乐仍跳过开头5秒，默认音量5。八目标已从公开依赖闭包逐字节重建，见 verification/public_software_rebuild.json；USB资格单列 verification/usb_wake_delivery.json。当前状态以根README及docs/current_status.md为准，下文原始测量记录按当时版本解释。

本次验证使用同一台 Windows 开发机和原 AX7020。源码在隔离新目录重建；未声称已在全新电脑、第二块板或新 SD 卡验证。路径中空格的软件/仿真重放也已实际通过。以下命令均从本目录根执行；所有输出目录须新建且不存在，脚本不会清理旧结果。

## 软件与硬件

```powershell
python software/rebuild.py --destination "D:/rvgame_sw_new" --compiler "D:/Xilinx/Vivado/2024.2/gnu/riscv/nt/riscv64-unknown-elf/bin/riscv64-unknown-elf-gcc.exe"
python hardware/rebuild.py --destination "D:/rvgame_hw_new" --vivado "D:/Xilinx/2025.2.1/Vivado/bin/vivado.bat"
python sim/replay.py --destination "D:/rvgame_sim_new" --vivado-bin "D:/Xilinx/2025.2.1/Vivado/bin"
```

编译器是 GCC13.3，安装目录中含2024.2不表示本工程仍用Vivado2024.2实现。硬件用2025.2.1，目标XC7Z020CLG400-2。不要自动升级IP或用其他编译器结果替代逐字节验证。软件包为五个基准stable、鸡蛋interactive、星际space、菜单menu四组冻结源，不依赖原工作区公共库。本次输入/底栏修复改变三个PS Python文件和菜单一个头文件，仅menu.bin变化；其他七目标的源、配方及二进制继承已核身份。新菜单已在含空格新目录独立逐字节重建；可用`--applications menu`单独复现，默认仍重建全部八目标。

软件8个目标的bin与运行包逐字节一致。硬件从82项输入完成工程/BD/IP、综合、布局布线、原验收脚本及bit/XSA/HWH导出。资源23447LUT/11576FF/126BRAM36/4DSP，WNS+0.033ns、WHS+0.057ns、WPWS+0.312ns。冷重建bit文件哈希不同，**尚未上板**；运行包保留原已验证硬件。仿真8组/10次通过，两个负例精确命中指定Fatal，详见sim/readme.md。

## 运行与证据重算

```powershell
python runtime/run.py
python verify_publication.py
python verification/recheck_final_visual.py --output "D:/rvgame_final_visual_new.json"
```

上述公开入口已离线复核通过，不连接板卡；当前两游戏复核所需的差异输入和原始帧随包保存。正常板端入口为 `sudo -E python3 runtime/run.py --start`，环境、单owner和停止操作见runtime/readme.md与rollback.md。之前独立入口板测绑定原应用版本，新增可选USB helper单独安装验证；完整公开清单没有冒称重新做过独立入口板测。入口不会自动安装系统服务，不代表新SD验证，原板owner仍运行时不要另启。

下面关于 recheck_metrics.py 和 recheck_ui.py 的段落说明历史验证过程；这些旧脚本可能需要未重复发布的旧二进制，不作为当前公开包的重跑入口。请使用上面的三条命令。

`verification/evidence_index.json`保存原始文件的来源、大小与SHA256；原始日志中的历史绝对路径只作来源信息。`recheck_metrics.py`从原始CPU文本、图形capacity计数和当前镜头版鸡蛋阶段meter重算，同时核对程序/硬件绑定，具体数量见输出收据。旧final_status_y的Alpha二进制与当前不同，当前Alpha使用showcase_final_g的对应证据。两份旧接收报告引用的共享脚本已在历史过程中被覆盖，原哈希和缺口如实保留；不声称保存了该脚本的旧字节。

`recheck_ui.py`用包内输入重算62份完整遥测、303份PCM CRC、控制/退出与默认和邻档窗口，并验证软件完整包30.21895秒确有进展。它是对既有板测记录的离线复核，不是再做一次硬件测试。硬件冷重建报告、已资格报告和新产物分目录放置。`skill/`是可复用的字节比较与PYNQ所有权方法，其8个行为案例及最新版实际文件检查见verification/skill_check.json。

## 文档复现

通用报告在用户提供的官方Word模板副本中填充，保留页眉、页脚、页尺寸及栏目；页数与本轮目视范围见verification/document_qa.json。AMD专项正文为design_report.md，amd_design_report.tex为可编辑XeLaTeX源，矢量图在figures/。在report目录运行两次 `xelatex -interaction=nonstopmode -halt-on-error amd_design_report.tex` 可更新PDF；本次使用TeX Live2025、Windows宋体/黑体及Times New Roman。结构/退休图是机制示意；两带时序图是冻结RTL的真实功能仿真采样，测试时钟和复制忙等待由测试平台指定，不代表板端DMA耗时。原始CSV、测试平台和工具日志位于verification/timing_illustration。

中英文海报沿用桌面AMD PPT官方模板，保留AMD标识；各1页16:9，PPTX可编辑，原比例PDF另附A4横向等比彩印PDF，不拉伸图文。模板示例学校、人员和实物图片没有作为本作品内容保留。目视核查记录按最终文件SHA保存，这不是独立末审或用户审定。
