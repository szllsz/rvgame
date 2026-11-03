# AMD 自主选题要求核对矩阵

依据：2026-11-03重新核对[官方完整指南](https://gitee.com/Vickyiii/fpgachina26-amd/blob/master/TRACK_GUIDE_2026.md)第3.3节和末尾海报要求，原文48151字节，SHA256 `aa8c1880538663e106ed88d8a70fc864e6faa68a652f907d9b86a034cdfd0753`。本目录为GitHub公开快照；比赛平台提交仍由作者完成。

| 条款 | 要求 | 交付入口 | 当前状态与验收条件 |
| --- | --- | --- | --- |
| 3.3.2 | 完整、可演示作品 | runtime/、report/design_report.md | native800_final_visual_a已安装；菜单／游戏自动板测通过。USB运行中失效于11月3日报告，重启恢复，原因待查；最终人工验收待完成 |
| 3.3.3.1 | 工具版本声明 | hardware/recipe.json、software/recipes.json | Vivado2025.2.1；GCC13.3来自2024.2目录，分别说明 |
| 3.3.3.1 | 真实模型协作 | report/model_collaboration.md、report/collaboration/ | 仅真实可见记录，隐藏推理/工具载荷不导出；事后摘要单列 |
| 3.3.3.2 | 板卡/器件与实测 | report/design_report.md、verification/ | AX7020 XC7Z020CLG400-2；已有板测与复核，人工体验另列 |
| 3.3.3.3 | GitHub/Gitee开源及可再分发协议 | LICENSE、licenses/、report/contribution_and_sources.md | 原创MIT，第三方保留原条款；按作者确认的配乐用途公开完整运行输入，未使用旧美术已清理 |
| 3.3.4 | 可测性能和基线 | report/metrics.json、design_report.md | 同版本同场景；历史多因素优化不当作单因素加速 |
| 3.3.4 | 合理资源使用 | hardware/、verification/ | 完整系统与CPU层级分列；不虚构功耗/能效 |
| 3.3.5.1 | 完整RTL/PS/RV源码 | hardware/inputs、software/source、runtime/package | 八程序冻结配方；PS源随运行包；检查无外部工作树依赖 |
| 3.3.5.1 | 验证/参考结果 | verification/、sim/ | 精选与当前字节匹配的原始/汇总/校验入口，保留关键失败 |
| 3.3.5.1 | 综合/实现报告 | hardware、verification/hardware/ | 冻结已板测实现报告与冷重建报告分开 |
| 3.3.5.1 | 独立上板程序包 | runtime/ | 已具备合适PYNQ环境可运行；不依赖原工作目录或经典游戏；不冒称新SD安装实测 |
| 3.3.5.1 | 可复现构建脚本 | software/rebuild.py、hardware/rebuild.py、sim/replay.py | 软件8目标逐字节重建；硬件从源冷重建通过；8组/10次仿真通过，新bit未上板 |
| 3.3.5.2 | Skill和README | skill/ | 8正负/边界案例及真实产物校验；通用PYNQ生命周期方法，无跨板实测虚构 |
| 3.3.5.3 | 背景/创新/框图/接口/优化/复现报告 | report/design_report.md、amd_design_report.tex/PDF | AMD专项与官方Word模板通用报告分别编写；本次更新后的页数/目视核查见verification/document_qa.json，真实照片待补 |
| 3.3.5.4 | 英文路径及目录映射 | README.md、清单脚本 | 源包路径小写；README.md/SKILL.md沿官方要求命名；不含中文/空格路径 |
| 末尾海报及用户最新通知 | 中文和英文各单页、AMD PPT模板、匿名A4彩印 | report/poster/ | 桌面官方模板保留；中英文各有PPTX与原比例PDF，另导出A4横向等比彩印PDF；实拍/最终人工审定待完成 |
| 用户提供上传页面 | MP4 AVC/H.264、3分钟内、300MB内 | report/manual_lab_checklist.md | 用户明确视频先不动；本轮未新增或修改视频、字幕、剪辑工程 |

各组别设计任务和提交内容一致，报名组别依据队员最高学历/年级由用户确认。未替用户推定组别、姓名或学校。高级/初级组同采用30/20/20/15/15的评分项权重；报告强调贡献和证据，不预估得分或保证获奖。

原官方目录是推荐结构；本目录采用hardware/software/runtime/verification/report/skill/licenses，以上明确映射。独立运行包必须有实际入口、环境依赖、完整输入和SHA验证，仅把工程复制到新目录不算已经运行验证。
