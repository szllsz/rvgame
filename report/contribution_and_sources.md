# 贡献与来源

## 贡献范围

本工程的技术贡献集中在一套完整图形 SoC 的核心机制与系统集成：RISC-V 执行及存储路径、二维图形加速、原生 800×600 分带提交与 DDR 显示协同、输入/PCM/应用切换运行时，以及独立像素、CRC、逐扫描计数和源码重建工具。五主级流水、受约束双指令路径、预测、旁路和缓存均可在随包实例中核对。

参赛者提供目标与硬件条件、交互反馈和最终采用决定；大模型参与源码分析、实现、调试和复现。关键设计问题的取舍与验证依据见 [工程决策、验证与大模型协作](model_collaboration.md)。下表区分本工程贡献与第三方复用，不以代码行数估算个人或模型的贡献比例。

| 内容 | 本项目贡献 | 复用边界 |
| --- | --- | --- |
| CPU和存储 | 软核RTL、提交/异常与取指归属、工作RAM/缓存和MMIO整合 | RISC-V ISA、AXI、AMD PS/DDR控制器不计原创 |
| 图形和显示 | TILE/位图/五档Alpha、裁剪、两带及完整帧提交、同场景比较 | Digilent视频编码IP保留原许可；显示标准不是原创 |
| 运行时 | 单owner、单输入写者、程序切换、PCM补给、CRC退休及未知排空保留 | 使用Linux、systemd、PYNQ及USB库，不称自研操作系统 |
| 应用负载 | C运行适配、游戏/UI绘制、测量和交互整合 | 鸡蛋兄弟仍有既有Brotato来源规则/素材；星际保留有限弹幕脚本与音乐来源，改名不改权属 |
| 验证与复现 | 按程序冻结源码、独立字节重建、黄金像素、CRC与连续扫描判定 | CoreMark算法和字体设计属于上游；哈希通过不代替语义/实物验收 |

## 来源登记

| 依赖 | 来源与版本 | 当前使用与许可证据 |
| --- | --- | --- |
| CoreMark | eembc/coremark `1f483d5b8316753a742cbf5590caf5bd0a4e4777` | 原算法、移植接口及LICENSE.md；保留Apache条款和商标限制 |
| fpganes | strigeus/fpganes `bb36b11b7ade5aa32f90a1fa55039cc6a37cb50a` | APU计算核心及GPL声明；自研外围适配不取消原许可 |
| Digilent | rgb2dvi:1.3 revision3 | 包内IP源文件头的BSD3条款及来源清单 |
| Taisei | taisei-project/taisei v1.4.6 | 实际代码/参考范围见冻结源，保留COPYING；不能用Taisei许可覆盖另来源音乐/脚本 |
| Kenney | Space Shooter Remastered | 当前战机与既有部分精灵采用CC0素材，原license.txt保留；不把AI新怪兽归到Kenney来源 |
| 星际新怪兽 | OpenAI imagegen生成，2026-11-03 | 原始透明PNG、认可预览、提示词与SHA在software/visual_assets；转换为RGB332精灵。属于AI协作成果，不冒称手绘、Kenney CC0或板端截图 |
| 字体 | Fusion Pixel Font、Source Han Sans、Source Sans3 | 实际字形子集，分别保留字体OFL/来源记录 |
| 音乐 | Power Punch与既有新版七重奏音源 | 仅星际战斗PCM去掉原曲前5秒，循环点同步平移；首页与鸡蛋音乐保持。转换/哈希/CRC证明来源和播放字节链，不证明公开再分发权 |
| 既有游戏图形和脚本 | 原素材/提取记录 | 鸡蛋兄弟生成C数组、关卡规则及星际有限脚本按实际来源登记，第三方内容不计自主原创 |
| 外部环境 | PYNQ3.1.2、AMD Vivado2025.2.1、GCC13.3等 | 最终以环境收据为准；不随包分发厂商工具或操作系统 |
| 主菜单光标 | 本轮自绘矢量几何；参考Windows常见指针形态 | 未将Microsoft系统.cur文件加入产品；SVG/位图生成过程和冻结C数组可追溯 |
| 当前云海菜单背景 | 用户提供的Wallpaper Engine项目SouredAppleClouds，Workshop 1948961570 | 本地界面采用居中裁切适配；不声明为自绘或CC0。本轮实拍和截图如实展示该界面，没有将原壁纸工程公开上传 |
| 实物照片与操作录像 | 项目作者现场拍摄，2026-11-03 | 原图与原视频保留；报告图框只裁剪构图，不改器件、接线、屏幕内容或测量数字 |
| 演示视频旁白 | GPT-SoVITS与MamboTTS模型 | 第一版13段旁白在本地合成，片尾注明AI旁白；不将软件代码许可等同于音色素材授权 |
| 数字时序图表达 | WaveDrom官方教程作为表达参考 | 实际使用ReportLab绘制XSim CSV采样；未下载或安装用户截图中的第三方Skill |

这里列的是来源事实与已有许可文件，未给混合工程统一重新授权。GitHub已有公开快照，其中未使用的旧美术数组已按发布范围清理；配乐按项目作者明确确认的作品伴奏用途保留。第三方代码、图片、PCM及素材数组分别记录来源，不因项目代码采用MIT而重新许可。

公开仓库为 https://github.com/szllsz/rvgame ，原创代码采用 MIT，第三方内容保留各自说明。比赛提交包包含最终云海菜单；公开仓库保留六卡片菜单资源配置，并同步按需加载、PCM 批量写入和 USB 查询退避。两者分别提供完整源码、构建脚本和文件清单，复现时使用同一包内的输入。

## 相近项目与比较

[PicoRV32](https://github.com/YosysHQ/picorv32)、[Project F](https://github.com/projf/projf-explore)分别帮助界定软核和FPGA图形已有工作。本作品使用自己的系统内同负载比较，不直接比较不同器件、时钟、编译器与外设条件的宣传数字。[Kenney官方源页](https://kenney.nl/assets/space-shooter-remastered)提供所用飞机图形；检索到的Godot完整游戏不适合作为本项目裸机实现依赖，没有复制其游戏代码，也没有称其为获奖作品。
