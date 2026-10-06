# 主地图输入与蓝图分工

## 映射生命周期

`AGameMainMapPlayerController` 在本地玩家 BeginPlay 时启用通用映射，并读取当前 GameState。控制器订阅 `AGameMainMapGameState::OnCurrentMapTypeChanged`，`SetCurrentMapType` 改变类型后切换对应映射。

| 地图类型 | 启用的映射 |
| --- | --- |
| None | IMC_Common |
| Base | IMC_Common + IMC_Base |
| Battle | IMC_Common + IMC_Battle |

在 `BP_GameMainMapPlayerController` 的类默认值中设置 Common/Base/Battle Input Mapping Context；通用优先级默认 0，地图映射默认 10。切换时只移除本控制器持有的模式映射，保留其他 UI、插件映射；按住的旧输入需要松开后才能在新映射中触发。重复设置相同地图类型不会重复广播。EndPlay 解绑状态并释放映射。

`OnInputStateReset` 是通用蓝图事件：失焦清键、切换地图类型、释放地图 UI 时通知蓝图结束未完成的输入。C++ 不声明沙盘查询、拖拽或选择节点。

## 控制器蓝图

资产：`/Game/System/Map/GameMainMap/BP_GameMainMapPlayerController`。

保留 `MouseEventGraph`，由 Enhanced Input 调用蓝图自己的函数：

- `IA_MouseLeft`：Started → `CommandMap_BeginPointer`；Completed → `CommandMap_ReleasePointer`；Canceled → `CommandMap_CancelPointer`。
- `CommandMap_Tick`：更新左键手势和中键拖拽。
- `CommandMap_QueryPointer` / `CommandMap_FindSandbox`：检查地图类型、房间就绪、UI 遮挡和地图命中。
- `IA_MouseMiddle`：优先沙盘拖拽；其他场景位置沿用自由相机旋转，Completed/Canceled 停止。
- `IA_SandboxZoom`：位于 IMC_Base，滚轮上/下分别输出 +1/-1；蓝图优先缩放沙盘，其他基地位置调用相机缩放。
- `CommandMap_SetRoomInput`：进入房间时暂时关闭原生 Actor 点击事件，离开时恢复原值，避免抬起选择重复触发。

阈值保存在 `CommandMapDragThreshold`（默认 6 个视口像素）。按下只记录目标，移动越过阈值后拖动地图内容；抬起时补算最后位移，只有未拖拽且松开在同一瓦片上才选择。底座和相机不随地图拖动，边界限制仍由网格插件负责。

FreeCameraSystem 的 `bEnableDefaultMouseRotationBindings` 在玩家相机上关闭，避免默认右键旋转与中键增强输入冲突。基地滚轮动作消费对应默认滚轮键，防止沙盘与相机同时缩放。GameAndUI 使用 `SetHideCursorDuringCapture(false)`，保证可见光标坐标可用于拖拽。

## 主地图函数库

C++：`Map/GameMainMap/GameMainMapLibrary`；蓝图分类“主地图”。

“获取主地图对象”一次输出 GameMode、GameState、玩家控制器、玩家状态和 Pawn；还提供各对象的独立获取节点与“获取主地图玩家相机”。节点使用调用者所属世界和 PlayerIndex（默认 0），不跨 PIE 世界缓存。无效上下文返回空引用；客户端没有权威 GameMode 时该输出为空。

“获取当前地图类型”返回当前 GameState 的 `EGameMainMapType`，不存在主地图 GameState 时返回 None。

## 维护与验证

`GameMainMapArchitecture -Inspect` 导出两个场景及控制器的蓝图结构；`-Validate` 编译正式蓝图并检查孤立引脚；`-Apply` 仅用于从旧结构迁移，已迁移时不重复改写。备份及报告位于 `Saved/MainMapArchitecture`。旧场景迁移入口转发到此工具，旧鼠标工具遇到新结构直接退出。

相关运行测试：`SilverChoir.Input.MainMapMappingLifecycle`、`SilverChoir.Input.CommandMapPointer`、`SilverChoir.Input.MiddleMouseCamera`、`SilverChoir.Input.MiddleMouseMotion`。鼠标测试在真实 Enhanced Input 帧之间检查结果。
2026-10-02 验证：SilverChoirEditor Development 编译成功；正式场景/控制器蓝图重载编译通过，无孤立引脚。映射与函数库 1 项、中键相机 2 项、指挥室 RuntimeFlow 1 项、CommandMapPointer 1 项、BlueprintLifecycle 1 项，共 6 项通过，测试报告 0 警告、0 失败。报告位于 `Saved/MainMapArchitecture/{MappingAndMiddle,RuntimeFlow,Pointer,BlueprintLifecycle}/index.json`。

RuntimeFlow 验证两次进入时平移阶段臂长/俯仰/偏航没有提前变化，并恢复两组不同的运行时俯仰范围。Pointer 通过实际按键与逐帧 Enhanced Input，验证点击抬起选择、黏着拖动标记、快速抬起、失焦/UI/退出取消、沙盘滚轮和相机隔离。BlueprintLifecycle 使用复制的控件蓝图延迟完成通知，验证场景等待与下一帧退出。测试退出时恢复临时状态。

引擎 Experimental ToolsetRegistry 原有启动报错仍在：命令行编辑器报告 EasyHouseBuilderEditor.SetWallOpenings 的工具注册问题，游戏测试报告缺少 PythonTestRunner。它们位于自动化测试开始前；本次目标测试全部通过，没有相关蓝图运行时错误。