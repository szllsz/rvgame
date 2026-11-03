# 证据入口与范围

- `evidence_index.json`：原始来源文件的内容绑定与历史脚本缺口，数量以清单为准。来源路径保留在索引中，读取本目录不需要原工作区。
- `software_rebuild.json`：8个RV32程序在新目录重编，bin逐字节一致。
- `simulation/result.json`及各数字日志：8组/10次XSim重放。负例必须命中指定断言；旧死锁组是受控复现。
- `hardware/qualified`：运行包已资格硬件的原实现报告；`hardware/cold_rebuild`：本轮从源重建报告与新bit/XSA/HWH，未上板。
- `ui_recheck.json`、`recheck_ui.py`：重算历史polish_c版62份遥测和303份PCM CRC，旧输入按SHA归档；不冒充新视觉版实测。
- `../report/metrics.json`：当前图表数据。`recheck_metrics.py`重算历史CPU/图形/鸡蛋数字；不变的CPU/图形继续绑定当前字节，更新游戏使用归档字节。
- `final_visual_recheck.json`、`recheck_final_visual.py`：当前星际与鸡蛋收据的独立离线帧率、主/辅助遥测、PCM CRC、精确payload与安全退休复核。星际各档连续不少于60秒；鸡蛋每档停留不少于35秒，同波10秒循环、各计量段约8秒，二者口径不同。
- `pointer_return_recheck.json`、`recheck_pointer_return.py`：历史42份CRC、24次安全切换与底栏像素复核；后来更改的菜单和游戏按原SHA归档。直接双击菜单另有24项检查、25次切换的audio5记录，指针实现与当前字节相同；不能替代人工手感。
- `runtime_packaging.json`：运行负载与已安装版一致；独立启动包装器与部署文档另列，不宣称新SD测试。
- `standalone_board.json`：当前final_visual独立runtime在原板新路径实际启动菜单、扫描前进、零下溢、SIGINT退休/三释放及正式版恢复，清单SHA一致；以standalone_status.json定位。旧收据和旧journal保留各自历史身份。
- `skill_check.json`：可复用检查器8种行为及最新版实际文件校验。
- `document_qa.json`：两份报告、两版海报、A4等比打印副本与矢量图的成品哈希和排版检查范围。
- `timing_illustration/`：冻结控制RTL的真实功能仿真CSV与测试平台；测试时钟和复制忙等待由测试平台指定，不是板端DMA耗时测量。

新星际UI的polish_a旧整包超时、polish_b观察器窗口不足及最终polish_c成功均保留。失败的恢复成功不被记为功能通过。完整工作区还保留旧板卡/工具失败，本目录精选直接支撑当前作品结论的证据。

旧集线器的精确设备组合已通过用户确认的普通及约1秒手柄重插，兼容服务已安装；ENOSPC和无效候选仍保留为历史。物理冷启动、长期休眠、听感、实拍及最终公开/提交留待人工，用户允许自动部分先收尾。completion_audit.json明确以本轮自动范围判定，不代表所有人工事项或比赛提交已完成。
