# 游戏时间系统

插件位于 `Plugins/GameTimeSystem`，使用方法见 [插件说明](../Plugins/GameTimeSystem/README.md)。

已经启用 GameInstance 子系统。蓝图常用流程：**创建游戏时间回调任务 → 设置业务参数 → 构造有效游戏日期 → 注册游戏时间回调任务**。回调蓝图继承 `UGTS_TimeTask` 并重载 **时间回调任务**。

设置入口：**项目设置 → Plugins → 游戏时间系统**。默认从 `2049-01-01 00:00:00` 以一倍速开始，主地图切换不会重置；请按游戏开局/读档逻辑设置初始日期。

## 本轮验证

2026-09-28：`SilverChoirEditor Win64 Development` 完整编译及链接通过。首轮遇到 UE 5.8 自动化枚举类型和蓝图生成类类型的编译差异，修正后构建成功；最终日志 `Saved/Logs/GameTimeSystemBuildFinal.log`。

编辑器进程执行 13 项测试全部通过，退出码 0；日志 `Saved/Logs/GameTimeSystemEditorTests.log`：

- 10 项插件核心与函数库测试：闰年/月/年跨越、分数时间精度、倍率和暂停、非法日期/倍率、日历上界、预约排序、相同时间顺序、跨过截止时间、取消、回调重入、自重新注册、公平预算、任务 GC 保活及释放、回调中暂停/关闭。
- 1 项实际蓝图测试 `GameTimeSystem.Blueprint.Callback`：在内存中创建任务子蓝图、编译覆盖事件，确认函数库世界上下文引脚自动隐藏且无需接线；通过独立 GameInstance 注册后，确实执行蓝图事件并读取正确的日历日期。没有保存测试蓝图资产。
- 2 项小队名称测试：表内顺序和名称释放复用、表耗尽后“第X小队”、编号冲突跳过、重复查询不消耗编号，以及缺失/空/错误类型表的回退。

真实游戏进程中 `GameTimeSystem.Runtime.GameInstanceTickerAndLibrary` 通过，退出码 0；日志 `Saved/Logs/GameTimeSystemRuntimeTests.log`。由实际 GameInstance 和 CoreTicker 推进，临时将 World TimeDilation 降为 0.001，仍能按插件倍率触发任务；插件暂停后多帧保持时间不变，函数库通过任务自身取得世界上下文。测试结束恢复原日期、倍率、暂停状态及世界时间膨胀。

测试使用 NullRHI；本次不涉及界面外观。没有执行完整打包、网络同步或跨进程存档测试；插件当前不提供网络/自动存档功能。

## 文本控件注册功能（2026-09-28）

新增函数库节点 **注册游戏时间文本控件**、**取消注册游戏时间文本控件**、**游戏时间文本控件是否已注册**、**格式化当前游戏时间**。注册接受 `UTextBlock` 与 `FText` 格式，例如 `{Year}年{Month}月{Day}日 {Hour}:{Minute}:{Second}`，具体占位符与 UMG Construct/Destruct 用法见插件说明。

子系统弱引用保存控件，通过管理器时间变化事件刷新；同控件重复注册只替换格式，文字不变不重复 SetText。每秒清理失效弱引用，暂停期间也可清理；显式设置日期时立即更新。

最终完整编译通过，日志 `Saved/Logs/GameTimeTextBuildFinal.log`。本轮 17 项相关测试最终均通过：

- 首轮编辑器检查中 15 项通过，日志 `Saved/Logs/GameTimeTextEditorTests.log`，包含四项新增文本测试（格式补零/星期/自定义文字、即时更新和跨天、暂停与手动设置、多控件与重复注册、取消、跨 GameInstance 拒绝、弱引用 GC 与退出清理），以及原有时间和任务测试。
- 首轮实际蓝图节点编译暴露出 const FText 引用参数不能直接使用未连接的默认值；已为两个格式节点补充 `AutoCreateRefTerm`。随后两项蓝图测试均通过，日志 `Saved/Logs/GameTimeTextBlueprintFinalTests.log`，验证默认 FText 正确、隐式世界上下文以及真实蓝图无错误/警告编译。
- 独立游戏进程测试通过，日志 `Saved/Logs/GameTimeTextRuntimeTests.log`：实际 CoreTicker 推进后 TextBlock 的显示值与当前游戏日期一致，暂停期间稳定，暂停时手动改时立即刷新，结束后取消注册并恢复原状态。

没有修改现有 UI 蓝图资产，业务界面可自行调用注册节点；没有执行完整打包或外观截图测试。
