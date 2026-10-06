# 可配置的固定动画智能对象交互

HMS 保留原有 Gameplay Interaction / StateTree 坐椅子流程，并新增固定进入动画配置。查找、占用、导航、对齐、播放、取消和释放由 `UHMS_SmartObjectInteractionComponent` 管理；动画与物体偏移由 `UHMS_SmartObjectInteractionProfile` 数据资产提供。运行模块不引用 `/Game` 下的测试角色、椅子或动画路径。

这一版用于验证替换进入动画。动画播放完成后恢复普通运动并释放槽位，不包含持续贴靠掩体、沿掩体移动或退出掩体动画。

## 当前测试

地图：`/Game/MVTest/Maps/L_MVThirdPersonTest`。

点击左下角“进入掩体测试”：角色寻找带 `HMS.CoverTest` Actor 标签的空闲椅子槽位，移动到入口，使用 Mover 输入对齐朝向，再播放进入动画。同一按钮在交互过程中变为“取消掩体测试”。WASD、地面导航、瞄准、蹲伏或布娃娃操作也会取消当前交互。界面显示接近、朝向对齐、播放、完成或失败原因。

测试资产位于 `/Game/MVTest/SmartObject/Cover`：

| 资产 | 用途 |
|---|---|
| `M_Cover_Entry` | `M_Cover` 的交互副本，开启根运动，使用动画首帧锁定根骨 |
| `AM_Cover_Entry` | 使用 `DefaultSlot` 的进入 Montage |
| `DA_CoverEntry` | 进入动画与对齐参数 |

原始 `/Game/CC5_Character/Female_Anim/Run/M_Cover` 保持原样。它长约 3.233 秒，根骨沿动画局部 Y 轴移动约 812.5 cm，原本未开启根运动。当前配置从 1.5 秒开始，根运动对齐窗口截止到约 2.167 秒，保留最后约 250 cm 的跑入及后续进入姿态。正常导航负责更远的距离。

固定进入使用 HMS 的进度式 Motion Warping Modifier：从动画根位移计算每个模拟步的进度，以进入起点和目标之间的位移完成对齐，避免用表现层角色位置反复修正时在窗口末端放大横向误差。它面向先对齐朝向、再直线进入的动画；弧线、转身或移动物体交互应使用后续的专门配置或原有 StateTree。动画结束时还会检查脚底位置与目标的水平误差，超过 15 cm 会提示对齐失败。

## 调整或新增交互

复制 `DA_CoverEntry` 后设置：

- `EntryMontage`：目标角色兼容骨架的进入动画，须开启根运动，并使用角色动画图中实际存在的 Slot。
- `EntryStartTime` / `WarpEndTime`：Montage 的秒数范围。结束时间应对应根骨位移结束，避免在静止尾段继续拉动角色。
- `TargetOffset`：相对智能对象槽位的最终脚底位置与人物朝向。
- `ApproachDistance`：从最终位置沿人物朝向的反方向退回多少厘米作为导航入口，应接近所选动画片段的根位移。
- `RequiredActorTag`：可选的 Actor 标签过滤；当前用于限定测试椅子。

`FindAndUseSmartObject()` 继续执行物体原有的 Gameplay Interaction StateTree。`FindAndUseSmartObjectWithProfile(Profile)` 是一次手动固定动画覆盖请求，会占用同一套 Smart Object 槽位，但不启动椅子的坐下 StateTree。它当前选择具有 Gameplay Interaction Behavior 的槽位。

接入新的 Mover Pawn 时：

1. 添加 HMS Smart Object Interaction、NavMover、MotionWarping 组件，并提供有效的控制器、导航与动画 Slot。玩家角色的 `bAutoStart` / `bAutoRepeat` 设为 false。
2. 交互接近期间允许正常导航输入；手动接管时调用 `AbortInteraction`。
3. `IsAligningProfileEntry()` 为 true 时，清空移动输入，把 `GetProfileTargetTransform()` 的 X 轴方向写入 Mover 朝向输入。不要直接瞬移或设置角色旋转。
4. `IsPlayingProfileEntry()` 为 true 时，清空普通移动与朝向输入，由 Mover Montage 和 Motion Warping 控制根运动。测试项目的 `AMWTestPawnBase::ProduceInput` 提供了接入示例。

后续扩展建议继续使用同一生命周期组件，在配置层增加按姿态或角度选择进入动画的 Chooser、保持姿态和退出动画。等这套接口稳定后，再把 Smart Object 与 StateTree 相关代码拆成单独模块，避免为每种家具复制控制器、寻路和释放逻辑。多人模式的请求授权和状态复制需要单独设计与验证；当前按钮验证的是本地测试流程。

此固定进入实现使用当前项目的同步 Mover 后端；异步 Chaos Mover 的模拟线程不执行这些运行时 Modifier，会在开始前返回明确的失败原因，避免静默播出没有对齐的动画。原有坐椅子的 Gameplay Interaction 路径不受此限制检查影响。

## 本次验证结果

UE 5.8 Development Editor 编译通过。在测试椅子东、南、西、北四个方向分别启动 PIE，均完成导航、朝向对齐与 `AM_Cover_Entry` 播放，最终水平落点误差小于 0.2 cm。接近途中取消、动画播放中取消以及取消后重新进入均通过，结束后槽位占用已释放。实际界面按钮也已点击验证。

该误差反映角色落点精度，不代表与椅子表面的身体接触精度；更换真实掩体后，应按物体表面调整 `TargetOffset`。测试完成后已恢复原出生点与编辑器后台性能设置。
