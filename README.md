# 自研RISC-V二维图形SoC

在 ALINX AX7020（XC7Z020CLG400-2）上运行的原生 800×600 交互作品：105 MHz RISC-V 软核、TILE／位图／Alpha 图形加速、DDR 显示、PCM 音频，以及手柄、触屏、鼠标输入。

主菜单集成 CPU 测评、三项图形实验、鸡蛋兄弟和星际弹幕。触屏单击选中、双击进入；鼠标左键进入、右键返回；应用第一层可点 SELECT 返回。两款交互应用支持 LT／RT 切换三档负载。

## 仓库内容

| 目录 | 内容 |
| --- | --- |
| [hardware](hardware) | RTL、XDC、IP 配置、源代码重建脚本与来源记录 |
| [software](software) | 八个 RV32 程序的源码、构建配置与素材 |
| [runtime](runtime/readme.md) | 完整 PS 源码、八个程序、已板测 bit/HWH、配乐和独立运行入口 |
| [sim](sim) | 功能仿真、测试输入与重放脚本 |
| [verification](verification) | 综合／实现报告、板端日志、性能／CRC 数据和复核脚本 |
| [report](report) | 技术报告、Word/PDF、双语海报和板端截图 |
| [skill](skill/skill.md) | 可复用的 PYNQ 产物校验及 DMA 生命周期方法 |
| [licenses](licenses/readme.md) | 第三方许可、字体与图形来源；配乐用途说明 |

目录与 [AMD 自主选题赛道指南](https://gitee.com/Vickyiii/fpgachina26-amd/blob/master/TRACK_GUIDE_2026.md) 的对应关系见 [要求矩阵](report/amd_requirements_matrix.md)。

## 复现

开发环境：**Vivado 2025.2.1、RISC-V GCC 13.3、Python 3**。从仓库根目录执行文件校验：

```sh
python verify_publication.py
python runtime/run.py
```

重建八个 RV32 程序，并与随附二进制核对。将工具路径替换为实际安装位置，输出目录使用新目录：

```sh
python software/rebuild.py \
  --destination out/software \
  --compiler /path/to/riscv64-unknown-elf-gcc
```

硬件构建、功能仿真与性能记录复核：

```sh
python hardware/rebuild.py \
  --destination out/hardware --vivado /path/to/vivado
python sim/replay.py \
  --destination out/sim --vivado-bin /path/to/vivado/bin
python verification/recheck_final_visual.py --output out/metrics.json
```

板端使用 AX7020 对应的 PYNQ/Linux 环境。完整步骤见 [复现说明](report/reproduction.md)，接线、启动和停止操作见 [运行说明](runtime/readme.md)。

## 性能与实机验证

| 项目 | 已记录的结果 | 测量范围 |
| --- | --- | --- |
| CoreMark | 约 371.852 分 | 105 MHz，CRC 核验 |
| CoreMark/MHz | 3.541 | 105 MHz 工作频率 |
| CoreMark/LUT | 0.05822 分/LUT | CPU 核心 6387 LUT；图形、显示和外设不计入分母 |
| 图块／位图／Alpha | 512／96／32 对象，严格 60 FPS | 连续窗口，逐扫描新帧判定 |
| 星际三挡 | 60.00／51.98／31.84 FPS | 各连续不少于 60 秒 |
| 鸡蛋三波 | 60.00／41.83／29.75 FPS | 64／85／100 对象；每波重复 10 秒场景，计量段约 8 秒 |

三档负载展示不同对象密度下的实际绘制吞吐。测试分别记录场景、计量窗口、新帧数和扫描数，生成图、板端截图及实物照片按来源标注。

手柄、触屏与鼠标经统一输入接口操作主菜单和应用，实物演示覆盖进入、返回及手柄启动后的输入接续。系统把程序加载、画面提交、音频和外设管理整合为完整运行流程，配套原始测量、自动核对与现场操作记录。

技术报告包含系统架构、实现方法、实物连接与性能分析：[PDF](report/competition_report.pdf)、[可编辑 Word](report/competition_report.docx)。[AMD 设计报告](report/amd_design_report.pdf) 进一步说明软硬件划分、优化过程、大模型协作与工程复现。

## 许可与来源

本项目原创代码采用 [MIT](LICENSE)。第三方代码、字体、图形与配乐按各自条款使用，来源见 [许可说明](licenses/readme.md) 和 [素材清单](report/contribution_and_sources.md)。
