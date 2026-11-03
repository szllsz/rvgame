# 正常停止与版本切换

前台通过 `python3 run.py --start` 运行时，按 Ctrl+C 发出正常停止请求。入口向唯一的子进程传递 SIGINT，并等待当前帧和传输结束、清理完成。

只有终端显示 `STANDALONE_OWNER_RETIRED`，且 `supervisor_state.json` 指向的运行目录内 `cleanup.json` 中 `pass`、`program_freed`、`display_freed`、`hp_restored` 均为真，才开始下一次加载。系统服务启动的程序应通过对应服务正常停止，并检查同样的清理结果。

替换或停止 USB 辅助服务的顺序是：先正常退出图形应用并确认资源释放，再停止辅助服务并等待其恢复接口绑定。不要在读取器仍工作时叠加另一套恢复流程。

切换到另一版本时，保留其完整 runtime 目录，先执行该版本的 `python run.py` 核对文件，再显式启动。不同目录之间不要混用 bit、HWH、程序及清单。退出失败时保留运行日志和状态，不强杀后立即重载硬件。
