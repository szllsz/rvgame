# 手柄启动后的 USB 停滞与恢复

本轮用户反馈包含两次失效：持续运行时三输入失效，断电后恢复；随后启动手柄时再次失效。最新版应用路径是 `native800_final_visual_a`，并非误启动旧应用。正式 release SHA 为 `bc2d28de675af0b292a1afc802993285d74168b2e36a3f25e578d1073708585b`，本次不改变八个应用、PL 或音乐。

## 故障证据

同一启动会话 `3b0ec8c9-16b5-4b7b-93c5-c85fb69493d8`，显示 owner 起初为 supervisor 390、child 541。鼠标、触屏、手柄 sysfs 节点仍在；手柄报告变为 stale。5 秒观察中 USB IRQ 固定在 137933，手柄 urbnum 继续增加，这只说明请求仍被提交，不能解释为有效输入增长。旧 guard 只按端口／devnum 变化触发，因此地址不变时没有重新预留。

原始证据在 `reports/usb_autonomous_20261102_a/wake_failure_20261103_a/`。一次受控 USB 主机重绑恢复鼠标与触屏，重新执行手柄优先预留后，用户确认三输入恢复；显示 owner 和启动会话没有变。对应一次恢复在 `reports/hub_touch_gamepad_20261102_a/one_shot_wake_failure_20261103_a/`。

旧 guard 停止时恢复鼠标附加接口，两次遇到约 71 秒控制超时。第一次发生在已停滞现场；第二次发生在切换候选时。失败保留，不能称作正常清理。第二次通过单独 USB 恢复收尾，见 `one_shot_helper_stop_20261103_b`。后续正式安装按“先让应用正常退休并关闭 USB 读取，再停止 helper”的顺序执行；候选已实际完成 stopped_restored。停 helper 时不能忽略正在读取手柄的应用。

## 本次改动

- 新的 stale／传输错误即使设备地址不变，也先暂时释放鼠标主接口，等待手柄有效解码后恢复鼠标。
- USB IRQ 连续 5 秒无进展且输入异常时，对精确 hub 与鼠标分别做标准 GET_DESCRIPTOR。两个请求均返回超时才允许重绑 `ci_hdrc.0`；正常静默而控制请求成功不重置主机。
- 仅允许已核准的五个 USB 节点及单 TT 拓扑。持有已有恢复互斥锁，最多每 5 分钟两次主机恢复，不与另一个恢复流程并发。
- 不访问 PL/MMIO，不重启显示作为 USB 恢复手段，不修改内核、USB 间隔或 GPIO。
- 启动期间等待应用就绪；应用实际开始读取 USB 后，再给出独立的 12 秒预留窗口，初始启动等待上限 90 秒。

候选 `software/usb_priority_20261103` 的真实记录捕获两次控制超时及自动重绑：3004.7 至 3014.9 秒、3016.4 至 3025.5 秒恢复鼠标；随后手柄地址变为 7，仍正常恢复。用户随后按要求关闭再启动手柄，确认三个输入可用。证据在 `reports/usb_priority_wake_20261103/candidate_b/observation_a.json`，不能把该范围扩展成长期休眠、所有设备组合或 EHCI 根因已修复。

候选 a 首次正式安装暴露启动计时窗口问题：输入已解码但预留窗口把应用加载时间算入，导致记录 bounded_wait_expired；原失败在 `installation_a/result.json` 保留。修正版本为 `software/usb_priority_20261103_b`，11 项本机行为检查见 `host_b/result.json`，正式安装结果以 `installation_b/result.json` 为准。

## 参考 Windows 的适用部分

本机实际枚举到 Intel USB 3.10／3.20 xHCI 控制器与 USB4 主机路由器；板端为 ChipIdea/EHCI 路径，硬件调度器不同。未在 PC 上移动同一套设备做对照，不能仅凭本机控制器名称证明 Windows 上的这组设备已实测通过。

微软建议按错误码区分原因，先取消未完成传输，再按局部 pipe、设备端口、重新枚举的顺序升级恢复，并协调同一时间只有一个恢复操作。当前现场没有确认 endpoint HALT，不能把带宽不足／总线停滞一律当成 clear-halt 问题。这里只借鉴错误分类、互斥和分级处理原则，不调用 Windows API。

参考原文：[USB pipe 错误恢复](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/how-to-recover-from-usb-pipe-errors)、[USB 带宽分配](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-bandwidth-allocation)、[选择性挂起](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-selective-suspend)。没有修改 PC USB 或电源设置，没有把全局禁用省电当作本板修复。

## 停止与回退

生产 helper 依赖 `Before=rvgame-unified-boot.service`，系统关机时会按逆序先停止应用。手动更换／回退也应先正常退休应用并核对 cleanup 的程序释放、显示释放、HP 恢复，再停止 helper，确认两个鼠标接口恢复后更换精确服务文件。不要在运行中的手柄读取器旁直接恢复鼠标附加接口。旧代码和服务备份保留；不需要批量删除文件。
