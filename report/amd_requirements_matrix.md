# AMD 自主选题要求核对矩阵

依据：项目已保存的官方完整指南第3.3节和末尾海报要求，原文48151字节，SHA256 `aa8c1880538663e106ed88d8a70fc864e6faa68a652f907d9b86a034cdfd0753`。本表于2026-11-03按实拍、视频和最新板端登记更新，未重新查询官网修订。已有GitHub公开快照；本轮文档更新是本地修订，不等于已经提交或推送全部后续变化。

| 条款 | 要求 | 交付入口 | 当前状态与验收条件 |
| --- | --- | --- | --- |
| 3.3.2 | 完整、可演示作品 | runtime/、report/design_report.md | 板端为native800_cloud_lazy_a及USB helper e；三输入正常操作和一次手柄按键启动已有用户确认，实拍录像已提供；冷启动、离线长运行和长休眠仍另验 |
| 3.3.3.1 | 工具版本声明 | hardware/recipe.json、software/recipes.json | Vivado2025.2.1；GCC13.3来自2024.2目录，分别说明 |
| 3.3.3.1 | 真实模型协作 | report/model_collaboration.md、report/collaboration/ | 仅真实可见记录，隐藏推理/工具载荷不导出；事后摘要单列 |
| 3.3.3.2 | 板卡/器件与实测 | report/design_report.md、verification/ | AX7020 XC7Z020CLG400-2；已有板测与复核，人工体验另列 |
| 3.3.3.3 | GitHub/Gitee开源及可再分发协议 | licenses/、report/contribution_and_sources.md | 已有szllsz/rvgame-native800公开快照及CI通过记录；第三方来源分别登记，后续云海菜单、运行层与文档更新仍需按最终公开范围同步 |
| 3.3.4 | 可测性能和基线 | report/metrics.json、design_report.md | 同版本同场景；历史多因素优化不当作单因素加速 |
| 3.3.4 | 合理资源使用 | hardware/、verification/ | 完整系统与CPU层级分列；不虚构功耗/能效 |
| 3.3.5.1 | 完整RTL/PS/RV源码 | hardware/inputs、software/source、runtime/package | 既有快照有八程序冻结配方及PS运行源；本轮只更新报告和实拍，最新板端运行层另有身份登记，不将旧冻结包当作云海最新版 |
| 3.3.5.1 | 验证/参考结果 | verification/、sim/ | 精选与当前字节匹配的原始/汇总/校验入口，保留关键失败 |
| 3.3.5.1 | 综合/实现报告 | hardware、verification/hardware/ | 冻结已板测实现报告与冷重建报告分开 |
| 3.3.5.1 | 独立上板程序包 | runtime/ | 已具备合适PYNQ环境可运行；不依赖原工作目录或经典游戏；不冒称新SD安装实测 |
| 3.3.5.1 | 可复现构建脚本 | software/rebuild.py、hardware/rebuild.py、sim/replay.py | 软件8目标逐字节重建；硬件从源冷重建通过；8组/10次仿真通过，新bit未上板 |
| 3.3.5.2 | Skill和README | skill/ | 8正负/边界案例及真实产物校验；通用PYNQ生命周期方法，无跨板实测虚构 |
| 3.3.5.3 | 背景/创新/框图/接口/优化/复现报告 | report/design_report.md、amd_design_report.tex/PDF | 通用报告沿用去年五部分结构，补实拍、MAX98357A及启动优化；专项报告独立更新。最新文档检查见report/evidence/document_refresh_20261103.json |
| 3.3.5.4 | 英文路径及目录映射 | README.md、清单脚本 | 源包路径小写；README.md/SKILL.md沿官方要求命名；不含中文/空格路径 |
| 用户补充AMD企业公告 | 官方模板；中文和英文各一页；PPTX；企业海报填写学校与队员 | report/poster/amd_poster_bilingual.pptx | 一个可编辑PPTX含两页，已填西安邮电大学、李圣哲及官方校徽；另附预览PNG和PDF |
| 用户提供上传页面 | 视频MP4 AVC/H.264，3分钟内、300MB内；匿名PDF报告50MB内；代码RAR200MB内 | submission_files.json及upload_checklist.md | 按最终文件大小、时长与哈希核对；授权书和开发板照片由用户完成 |

各组别设计任务和提交内容一致，报名组别依据队员最高学历/年级由用户确认。未替用户推定组别、姓名或学校。高级/初级组同采用30/20/20/15/15的评分项权重；报告强调贡献和证据，不预估得分或保证获奖。

原官方目录是推荐结构；本目录采用hardware/software/runtime/verification/report/skill/licenses，以上明确映射。独立运行包必须有实际入口、环境依赖、完整输入和SHA验证，仅把工程复制到新目录不算已经运行验证。
