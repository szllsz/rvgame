# 基于自研 RISC-V 的二维图形加速与实时交互 SoC

在 ALINX AX7020（XC7Z020CLG400-2）上运行的原生 800×600 交互作品：105 MHz RISC-V 软核、TILE／位图／Alpha 图形加速、DDR 显示、PCM 音频，以及手柄、触屏、鼠标输入。

当前代码对应 `native800_final_visual_a`（2026-11-03）。六张入口卡片包含 CPU 测评、三项图形实验、鸡蛋兄弟和星际弹幕。触屏单击选中、双击进入；鼠标左键进入、右键返回；应用第一层可点 SELECT 返回。两款展示使用 LT／RT 选择三挡；默认音量 5，星际战斗音乐跳过开头 5 秒。

## 仓库内容

| 目录 | 内容 |
| --- | --- |
| [hardware](hardware) | RTL、XDC、IP 配置、源代码重建脚本与来源记录 |
| [software](software) | 八个 RV32 程序的实际编译依赖、固定配方和素材数组 |
| [runtime](runtime/readme.md) | 完整 PS 源码、八个程序、已板测 bit/HWH、配乐和独立运行入口 |
| [sim](sim) | 八组仿真输入及十次执行的重放配方 |
| [verification](verification) | 综合／实现报告、板端日志、性能／CRC 数据和复核脚本 |
| [report](report) | 技术报告、Word/PDF、双语海报和板端截图 |
| [skill](skill/skill.md) | 可复用的 PYNQ 产物校验及 DMA 生命周期方法 |
| [licenses](licenses/readme.md) | 第三方许可、字体与图形来源；配乐用途说明 |

对应 [AMD 自主选题赛道指南](https://gitee.com/Vickyiii/fpgachina26-amd/blob/master/TRACK_GUIDE_2026.md) 的映射见 [要求矩阵](report/amd_requirements_matrix.md)。本仓库公开不代表已经向比赛平台提交。

## 复现

只检查文件和配置，不连接开发板：

```sh
python verify_publication.py
python runtime/run.py
```

使用 GCC 13.3 RISC-V 工具链重建八个程序；输出目录必须尚不存在：

```sh
python software/rebuild.py --destination /path/to/new-output --compiler /path/to/riscv64-unknown-elf-gcc
```

发布源码已去掉未使用的旧菜单图标和旧画面数组。八个程序在隔离输出目录重新构建，均与已安装版二进制逐字节一致；收据见 [公开源码重建](verification/public_software_rebuild.json)。原有目录名 `touhou`／`scarlet` 是接口兼容名称，当前星际画面使用 Kenney 素材和 AI 生成怪兽，详见 [来源说明](report/contribution_and_sources.md)。

硬件冻结配方使用 Vivado **2025.2.1**；软件编译器虽放在本机 2024.2 工具目录，其版本是 GCC 13.3。不要仅按目录名判断版本：

```sh
python hardware/rebuild.py --destination /path/to/new-hardware --vivado /path/to/Vivado/2025.2/bin/vivado
python sim/replay.py --destination /path/to/new-sim --vivado-bin /path/to/Vivado/2025.2/bin
python verification/recheck_final_visual.py --output /path/to/new-check.json
```

硬件已有原环境从源综合、实现及导出记录；重新生成的 bit 需要重新上板验证。板端运行环境和正常退休流程见 [runtime/readme.md](runtime/readme.md)，本仓库不是完整 SD 镜像。

## 测试结果与待办

| 项目 | 已记录的结果 | 测量范围 |
| --- | --- | --- |
| CoreMark | 约 371.852 分 | 105 MHz，CRC 核验 |
| 图块／位图／Alpha | 512／96／32 对象，严格 60 FPS | 连续窗口，逐扫描新帧判定 |
| 星际三挡 | 60.00／51.98／31.84 FPS | 各连续不少于 60 秒 |
| 鸡蛋三波 | 60.00／41.83／29.75 FPS | 64／85／100 对象；每波重复 10 秒场景，计量段约 8 秒 |

鸡蛋的短计量段不能当作连续 60 秒成绩；显示刷新率也不等于新帧产出率。新生成画面、真实板端截图和实物照片分别标注。

旧集线器曾出现运行中及手柄启动后三输入失效。2026-11-03 新增同地址输入异常检测、精确拓扑下有次数限制的 USB 恢复，并修正启动等待窗口；候选实际恢复两次总线停滞，手柄重启和最终三种操作进入／返回均获人工确认。服务已持久安装，长期休眠与物理冷启动仍待验收。截图底部彩色短线已确认是自动测试诊断数据，新报告排版避开该区域，原图保留。详见 [当前状态](docs/current_status.md)。

报告已参考上一年度技术文档重写为五大部分、20 页，含 13 张图：[Word](report/competition_report.docx)、[PDF](report/competition_report.pdf)、[正文源](report/technical_report.md)。原 AMD 专项材料与海报保留作补充，最新文字和问题状态以新报告为准。历史收据保留当时输入哈希，当前公开文件以 `publication_manifest.json` 为准。

## 许可与来源

本项目原创代码采用 [MIT](LICENSE)。第三方代码、字体、图形和配乐分别保留原来源与适用条款，MIT 不重新许可这些内容；详见 [licenses/readme.md](licenses/readme.md)。配乐按项目作者明确确认的作品伴奏用途随运行包保留。仓库不包含原始工作区、个人设备序列号、登录凭据、厂商工具安装包或旧版本整套素材。
