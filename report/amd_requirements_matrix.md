# AMD 自主选题交付对应表

依据 AMD 赛道指南第 3.3.5 节及企业材料通知，按交付内容组织如下。目录采用英文命名，顶层 README 提供官方推荐目录与本工程目录的映射。

| 要求 | 交付位置 |
| --- | --- |
| RTL、IP 与约束 | hardware/inputs |
| RV32 程序源码与完整素材 | software/source |
| PS 运行源码、板端程序与启动说明 | runtime |
| 可复现构建脚本 | hardware/rebuild.py、software/rebuild.py |
| 仿真脚本、输入与预期结果 | sim |
| 资源、时序与实现报告 | verification/hardware |
| 原始测量与黄金参考复核 | verification/data、verification/recheck_metrics.py、verification/recheck_final_visual.py |
| 独立重建验证记录 | verification/reproduction.json |
| 通用技能、使用方法及失效条件 | skill |
| 背景、创新、架构、接口、优化及复现说明 | report/amd_design_report.pdf |
| 大模型协作与技能提炼过程 | report/model_collaboration.md、report/collaboration |
| 开源项目与第三方来源 | https://github.com/szllsz/rvgame、licenses、report/contribution_and_sources.md |

## 设计报告内容对应

第 3.3.5.3 节列出设计报告的七项内容，没有要求在工程仓库内再附一份官网通用报告。本仓库以 `amd_design_report.pdf` 为唯一设计报告；`design_report.md` 和 `amd_design_report.tex` 是同一报告的编辑源稿。

| 指南要求 | 报告位置 |
| --- | --- |
| 选题背景与创新点 | 第 1 章 |
| 设计原理与功能框图 | 第 2 章；第 3 章展开核心算法 |
| 软硬件划分依据与接口设计 | 第 4 章 |
| 优化过程、性能与资源对比 | 第 5 章 |
| 大模型提示词、回答与纠错过程 | 第 6 章及 collaboration 原文节选 |
| 技能提炼、有效性与适用边界 | 第 7 章及 skill/README.md |
| 从零复现步骤 | 第 8 章及 reproduction.md |

第 3.3.5.4 节的目录树为推荐结构，README 已给出实际目录映射。工程路径采用英文命名；技能包保留独立 `skill/` 入口和验证脚本。

企业海报为 `poster/amd_poster_bilingual.pptx`，按本次企业通知提供中文、英文两页。指南第六节另述决赛现场的英文 A4 打印海报，现场版按对应通知准备。
