# FreeCameraSystem 自由相机插件

从 `D:/UE_workspace/SilverChoir/Plugins/G_Framework` 的 GFrameworkCore 相机系统提取，适配 UE 5.8。
独立 Runtime 模块，仅依赖引擎 Core / CoreUObject / Engine / InputCore / ApplicationCore。

## 结构与移植范围

| 原实现 | 独立实现 | 职责 |
|---|---|---|
| APlayerCameraPawn | AFCS_FreeCameraPawn | 平移、旋转、缩放、边缘滚动、边界限制、机位过渡 |
| UPlayerCameraConfigDataAsset | UFCS_CameraConfigDataAsset | 全部原相机配置项 |
| FPlayerCameraState | FFCS_CameraState | 位置、Yaw、Pitch、臂长与过渡速度 |
| GFrameworkCoreSubsystem 相机注册 | UFCS_FreeCameraSubsystem | 当前 World 相机注册和查找 |
| GFrameworkCoreBFL 相机节点 | UFCS_FreeCameraBlueprintLibrary | 无需持有 Pawn 的蓝图控制接口 |

相机仍是原系统的俯视/策略游戏自由相机：移动沿水平面，滚轮改变弹簧臂长度；机位过渡可改变高度。
保留按缩放距离调整移动速度、弹簧臂碰撞开关、移动/旋转延迟、状态完成回调、复制机位到剪贴板。
不包含原框架的拖拽、按钮、场景导演、子关卡控制和可见性模块。

原来的“写入当前场景进入/销毁机位”直接依赖 GFrameworkSceneManager，未带入独立插件。
现在通过 `GetCurrentCameraState` 获取机位，由项目自己的数据保存；通过 `MoveToCameraState` 恢复并接收完成回调。
再次调用 MoveToCameraState 会替换之前的过渡；CancelCameraStateMove 取消过渡且不触发完成回调。

## 当前工程接入

插件已在 CharmbeastTavern.uproject 启用。编译后重新打开编辑器即可找到“自由相机Pawn”和“自由相机配置”。

1. 创建 `AFCS_FreeCameraPawn` 的蓝图子类，例如 `BP_FreeCamera`。
2. 内容浏览器 → 杂项 → 数据资产 → 选择 `UFCS_CameraConfigDataAsset`（自由相机配置），设置速度、俯仰、臂长、边界等。
3. 将此资产赋给 BP_FreeCamera 的“相机配置文件”。不配置时沿用原系统默认参数。
4. 在游戏地图使用的 GameMode 蓝图中，将 **Default Pawn Class** 设置为 BP_FreeCamera，地图放置 PlayerStart。
   当前项目的 `ACTGameScenePlayerController` 已支持 Game And UI 输入和鼠标显示，可直接搭配使用。
5. 若选择手动放置相机 Pawn，应设置该实例的 Auto Possess Player=Player 0，并避免 GameMode 再生成第二个 Pawn。

没有修改现有地图、GameMode 默认 Pawn 或主菜单配置。原 G_Framework 的 BP_PlayerCamera 与 DA_DefaultCameraConfig 二进制资产
依赖旧模块路径，未直接复制；本插件的原生默认输入和配置类不需要这些资产即可工作。

## 默认输入

| 输入 | 功能 |
|---|---|
| W / S | 前后平移 |
| A / D | 左右平移 |
| Q / E | 水平旋转 |
| 鼠标滚轮 | 缩放 |
| 按住鼠标右键拖动 | 水平及俯仰旋转 |

默认绑定由 Pawn 的 InputComponent 提供，不修改项目输入设置。
若用 Enhanced Input 或蓝图自行绑定，把 Pawn 蓝图中的“启用默认键鼠绑定”关闭，再调用
MoveForward / MoveRight / RotateCamera / RotatePitch / ZoomCamera / TriggerMouseRotate 等接口。
自行传入鼠标增量时调用 RotateCameraByMouseDelta，不要同时启用内部鼠标旋转采样，以免重复旋转。
平移和键盘旋转函数按帧调用，内部使用 DeltaSeconds；滚轮缩放是步进操作。
UI 消费输入时不会收到默认按键事件；边缘移动独立于按键，打开全屏菜单时可调用 SetEdgeScrollEnabled(false)。

## 生命周期与查询

- BeginPlay 应用配置并注册到当前 World；EndPlay 注销、取消回调并恢复鼠标。
- 解除 Possess 时清空默认按键和鼠标旋转状态。
- 默认不自动抢占 Player 0，请通过 GameMode 或明确的 Possess 使用。
- 函数库操作该 World 最近注册的相机；多个相机时可显式 RegisterFreeCamera 切换函数库目标，或持有 Pawn 引用直接调用。
- 注册并不等于 Possess 或 SetViewTarget。删除当前注册相机后需显式注册其他相机。
- 注册表随 World 清理；不依赖 GameInstance 中的 G_Framework 对象。
- 仅受本地玩家控制的相机读取鼠标/边缘输入；当前功能未实现联网复制，不保证分屏边缘滚动区域。

## 验证

2026-09-16：UE 5.8 Win64 Development 的 Editor、Game 目标均构建成功。
相机自动化测试 1 项通过，0 失败、0 测试警告。
报告：项目 `Saved/Automation/FreeCameraSystem/index.json`；日志：`Saved/Logs/FreeCameraSystem.log`。

自动化 `FreeCameraSystem.Camera.ConfigMovementAndLifecycle` 在游戏 World 中检查配置、移动、缩放、边界、状态过渡、
输入绑定和 Possess/EndPlay 清理。NullRHI 验证逻辑，不代表真实视口鼠标捕获、UI 焦点或渲染手感已经验收。

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  'D:\UE_workspace\CharmbeastTavern\CharmbeastTavern.uproject' /Engine/Maps/Entry -game -NullRHI -unattended -nosound `
  '-ExecCmds=Automation RunTests FreeCameraSystem' '-TestExit=Automation Test Queue Empty' `
  '-ReportExportPath=D:\UE_workspace\CharmbeastTavern\Saved\Automation\FreeCameraSystem'
```
