# 自研RISC-V二维图形SoC

在 ALINX AX7020（XC7Z020CLG400-2）上运行的原生 800×600 交互作品：105 MHz RISC-V 软核、TILE／位图／Alpha 图形加速、DDR 显示、PCM 音频，以及手柄、触屏、鼠标输入。

公开代码快照对应 `native800_final_visual_a`（2026-11-03）。六张入口卡片包含 CPU 测评、三项图形实验、鸡蛋兄弟和星际弹幕。触屏单击选中、双击进入；鼠标左键进入、右键返回；应用第一层可点 SELECT 返回。两款展示使用 LT／RT 选择三挡。

技术材料已更新至实拍与最终菜单阶段。后续板端采用 `native800_cloud_lazy_a` 和 USB helper e，最新提交包已另行从源码重建核对；本仓库的公开运行快照仍保持上面的冻结版本。报告中的新菜单、启动优化和现场录像不代表该公开快照已经包含后续壁纸与运行层，版本依据见 [复现说明](report/reproduction.md) 和 `report/evidence/`。

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

## 性能与实机验证

| 项目 | 已记录的结果 | 测量范围 |
| --- | --- | --- |
| CoreMark | 约 371.852 分 | 105 MHz，CRC 核验 |
| CoreMark/MHz | 3.541 | 105 MHz 工作频率 |
| CoreMark/LUT | 0.01586 分/LUT | 完整 PL 23447 LUT；核心口径另列于报告 |
| 图块／位图／Alpha | 512／96／32 对象，严格 60 FPS | 连续窗口，逐扫描新帧判定 |
| 星际三挡 | 60.00／51.98／31.84 FPS | 各连续不少于 60 秒 |
| 鸡蛋三波 | 60.00／41.83／29.75 FPS | 64／85／100 对象；每波重复 10 秒场景，计量段约 8 秒 |

三档负载展示不同对象密度下的实际绘制吞吐。测试分别记录场景、计量窗口、新帧数和扫描数，生成图、板端截图及实物照片按来源标注。

手柄、触屏与鼠标经统一输入接口操作主菜单和应用，实物演示覆盖进入、返回及手柄启动后的输入接续。系统把程序加载、画面提交、音频和外设管理整合为完整运行流程，配套原始测量、自动核对与现场操作记录。

官网技术报告参考上一年度技术文档的五部分结构，现为 22 页、18 张图，已补入整机、USB 与音频连接实拍：[Word](report/competition_report.docx)、[PDF](report/competition_report.pdf)、[正文源](report/technical_report.md)。[AMD 设计报告](report/amd_design_report.pdf) 为 12 页，按指南第 3.3.5.3 节七项要点组织。两份公开 PDF 保持匿名；含学校与姓名的最新中英文两页企业海报只保存在本地提交材料中，仓库原匿名海报作为历史附件保留。历史收据保留当时输入哈希，当前公开文件以 `publication_manifest.json` 为准。

## 许可与来源

本项目原创代码采用 [MIT](LICENSE)。第三方代码、字体、图形和配乐分别保留原来源与适用条款，MIT 不重新许可这些内容；详见 [licenses/readme.md](licenses/readme.md)。配乐按项目作者明确确认的作品伴奏用途随运行包保留。仓库不包含原始工作区、个人设备序列号、登录凭据、厂商工具安装包或旧版本整套素材。
