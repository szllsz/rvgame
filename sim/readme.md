# 原生 800 仿真复现

使用 Vivado 2025.2.1 自带 XSim：

```powershell
python sim/replay.py --destination "D:/rvgame_sim_new" --vivado-bin "D:/Xilinx/2025.2.1/Vivado/bin"
```

输出目录必须不存在。输入逐项核对 SHA256，再复制到隔离目录编译；不使用旧 `xsim.dir`、WDB、DCP 或原工作树。8 组冻结用例包含 10 次仿真：800×600 全像素和总线背压、同 RTL 的 640 尾部兼容、并发 PCM 及坏样本拒绝、软件像素 CDC/末尾与缺初始化拒绝、旧 HOLD 死锁复现和两种先取消再排空路径。

两个负例必须命中且只命中指定 Fatal 断言；编译失败、其他 Fatal/ERROR 或正例 PASS 标记不能算预期拒绝。本机 XSim 在 `$fatal` 后仍返回 0，因此不能只根据进程退出码判断仿真成功。HOLD 组的旧死锁是受控 testbench 的预期观察，不是要求板上复现死锁。所有案例均为数字仿真，不替代真实 DDR、HDMI、音频或完整 CPU 指令集验证。原输入来源与哈希见 `fixtures.json`，本轮独立重放收据见 `../verification/simulation/`。

工具行为参考：[SVUnit issue 282](https://github.com/svunit/svunit/issues/282) 报告了同类退出码现象；本包的判断以保留的本机 2025.2.1 日志为准，不推断其他版本已修复。
