# 工程复现

## 开发环境

| 项目 | 要求 |
| --- | --- |
| 开发板 | ALINX AX7020，XC7Z020CLG400-2 |
| FPGA工具 | Vivado 2025.2.1 |
| RV32编译器 | AMD RISC-V GCC 13.3.0，支持RV32I/Zmmul |
| 主机脚本 | Python 3.12；依赖见requirements-host.txt |
| 板端系统 | AX7020对应的Linux/PYNQ环境，依赖与接线见runtime/readme.md |

下列命令从工程根目录执行。工具路径替换为实际安装位置，构建输出使用新目录。

主机复核依赖安装：

```sh
python -m pip install -r requirements-host.txt
```

逐字节重建验证使用 AMD 工具链中的 `riscv32-xilinx-elf-gcc` 13.3.0（可通过 `riscv64-unknown-elf-gcc` 入口调用），以 `--version` 输出确认。其他发行版的同版本 GCC 不保证产生相同二进制。

## 程序与硬件构建

```sh
python software/rebuild.py \
  --destination out/software \
  --compiler /path/to/riscv64-unknown-elf-gcc
python hardware/rebuild.py \
  --destination out/hardware --vivado /path/to/vivado
python sim/replay.py \
  --destination out/sim --vivado-bin /path/to/vivado/bin
```

软件脚本构建全部八个程序，并比较输出二进制；添加 `--applications menu` 可单独构建菜单。硬件脚本依据RTL、约束、IP与初始化数据重建工程，生成资源、时序及硬件产物。仿真覆盖正常流程和异常处理，测试说明见 `sim/readme.md`。

## 文件与结果校验

```sh
python verify_publication.py
python runtime/run.py
python verification/recheck_metrics.py --output out/metrics.json
python verification/recheck_final_visual.py --output out/games.json
```

文件校验与测量记录复算在主机执行。复算读取随包保存的数据；板端运行使用下面的独立步骤。

## 可移植工具自检

```sh
python skill/test_verify_artifacts.py --output out/skill
python runtime/usb_priority/test_guard.py --output out/usb
```

重建与复核结果见 `verification/reproduction.json`。软件从包内完整依赖重建；硬件从 RTL、IP 和约束重建；测量复算读取原始板端数据。重新生成的 bit 未在本轮加载到开发板。

## 板端运行

将完整 `runtime` 目录复制到开发板，按 `runtime/readme.md` 配置Python依赖、输入服务和音频链路。已有应用先正常退出，再启动统一入口：

```sh
sudo -E python3 runtime/run.py --start
```

程序从主菜单进入各应用。操作方式见 `runtime/controls.md`，停止与资源释放步骤见 `runtime/rollback.md`。直接演示使用随包提供的bit/HWH；重新构建的硬件按相同流程完成上板测试后使用。

## 报告与图表

技术报告包含Word和PDF，AMD设计报告同时提供Markdown与XeLaTeX源文件。结构图位于 `report/figures`，实物照片位于 `report/photos`。编辑AMD正文后，在 `report` 目录执行两次：

```sh
xelatex -interaction=nonstopmode -halt-on-error amd_design_report.tex
```

文档工具为TeX Live 2025，使用宋体、黑体和Times New Roman。实验数据的程序、硬件及测量条件由验证目录中的清单关联。

## 运行配置

仓库提供六入口菜单和八个RV32程序。报告中的云海菜单及启动优化属于演示配置；仓库默认运行包采用随附的菜单资源与加载流程。复现文件以 `runtime/release_manifest.json` 为准，源码与二进制对应关系见 `software/recipes.json`。
