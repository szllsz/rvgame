# 复核入口

`data/cpu` 保存两轮 CoreMark 的周期计数、CRC 与程序清单；`data/graphics` 保存 TILE、位图、Alpha 的连续窗口计数。`data/star`、`data/egg` 保存两款应用的原始图像、音频、包记录及独立参考输入。`hardware/qualified` 保存已验证硬件的资源和时序报告。

```sh
python -m pip install -r requirements-host.txt
python verification/recheck_metrics.py --output out/metrics.json
python verification/recheck_final_visual.py --output out/games.json
```

检查器先核对测量时的程序及硬件身份，再复算指标。CoreMark/LUT 使用 CPU 核心的 6387 LUT，整套 SoC 资源另列。历史采集依赖若与当前菜单不同，以 `data/qualified_runtime` 中明确保存的对应输入复核，不把历史测量标为新菜单测试。

`reproduction.json` 记录本包的主机环境、八程序重建、功能仿真、硬件构建及便携自检结果。本轮构建和离线复算不操作开发板；随附硬件产物与原始实测记录保持原有版本对应关系。

`publication_sync.json` 记录 2026-11-04 公开版同步后的主机检查：八程序逐字节重建、原始数据复算、技能自检、USB 边界和运行层契约检查。公开菜单与对应点击区域成套保留；按需加载、PCM 批量写入和 USB 查询退避已同步。运行层自检命令为 `python verification/test_runtime_sync.py --output out/runtime-sync`，使用模拟接口，不连接硬件。
