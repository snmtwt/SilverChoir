# NPCLevel 寻座与坐下链路

## 样例的实际执行链

`AIC_NPC_SmartObject` 通过 `StateTreeAIComponent` 运行
`ST_NPC_SandboxCharacter_SmartObject`。外层 StateTree 执行：

1. `STT_FindSmartObject`：以 NPC 为中心在 2000 cm 半范围盒中查找
   `GameplayInteractionSmartObjectBehaviorDefinition`，默认选最近槽位。
2. `STT_ClaimSlot`：只对 `Free` 槽位调用 `MarkSlotAsClaimed`；如果竞争失败，
   在同一长椅上寻找另一个空槽位。
3. `STT_UseSmartObject`：把 Claim Handle 交给 Gameplay Interaction，开始长椅自己的
   `ST_SmartObject_Bench`。
4. 使用完成后释放 Claim，添加 5 秒 `Sit` 冷却，再回到巡逻。

## 长椅为什么能丝滑对齐

`SO_BenchDefinition` 有两个座位，横向偏移分别是 `Y=45` 和 `Y=-45` cm。
每个座位还有一个 `X=48` cm 的 Entrance Annotation，并启用地面投射、
轨迹和槽位重叠验证。

`ST_SmartObject_Bench` 的内部流程是：

1. 查找合法 Entrance。
2. `MoveTo` 到 Entrance，接受半径为 10 cm。移动时同时面向长椅，
   忽略 NPC 与长椅的碰撞，并持续评估入座动画。
3. 当路径距离小于 300 cm 且速度不低于 10 cm/s 时，根据长椅局部坐标中
   NPC 的接近角选择前/左/右/后左/后右入座 Montage，再用 Pose Search
   输出 Cost 和最佳 Start Time。Cost 不大于 1 才会切入。
4. 播放时把 Claim Handle 的世界 Slot Transform 写入名为 `SmartObject` 的
   Motion Warping Target，所以根运动不是瞬移，而是被动画窗口平滑拉到坐位。
5. 入座后播放坐姿循环 10±3 秒，然后播放起身 Montage，恢复碰撞并释放槽位。

## HMS C++ 组件

`UHMS_SmartObjectInteractionComponent` 替代外层蓝图的 Find/Claim/Use 和生命周期代码。
它只依赖 Smart Object 中的 Gameplay Interaction Behavior，因此可以复用在椅子、
工作台、门和其他交互物上。`AHMS_SmartObjectAIController` 是带该组件的最小宿主。

动画选择、Motion Warping 和循环/退出仍保留在 Smart Object 自己的数据资产中，
这样 HMS 不需要硬编码 Game Animation Sample 的骨架和 Montage 路径。

### 接入方式

1. 给现有 AIController 添加 `HMS Smart Object Interaction` 组件，或者直接使用
   `AHMS_SmartObjectAIController`。
2. Pawn 必须由 `AAIController` 控制，并提供能执行 `MoveTo` 的导航移动接口；样例的
   Mover Pawn 已有 `NavMover`。
3. Pawn 继续保留 `AC_SmartObjectAnimation`、`MotionWarping` 和兼容的 AnimInstance，
   以复用样例的 Chooser、Pose Search 与入座 Montage。
4. Smart Object Definition 的用户过滤器必须接受
   `SmartObject.ObjectType.NPC`。组件在 `UserTags` 为空时会自动添加该标签。
5. 关卡需要有效的 Recast NavMesh，长椅 Actor 的原点应与样例一致落在地面 `Z=0`；
   不要用包围盒 `Snap To Ground` 放置该蓝图，否则座位入口会被整体抬高。

可在蓝图或 C++ 中调用 `FindAndUseSmartObject`、`AbortInteraction` 和
`ResetInteraction`，并监听 `OnSlotClaimed`、`OnInteractionStateChanged`、
`OnInteractionFinished`。搜索范围、最近/最远/随机选择、成功冷却与失败重试均可配置。

## L_MVThirdPersonTest 测试配置

关卡中的 `HMS/SeatInteractionTest` 文件夹包含测试长椅和 Mover NPC；
`HMS/Navigation` 中的 NavMesh Bounds 覆盖测试区域。地板已设为静态导航几何体并已
构建路径。当前 NPC 先使用样例 `AIC_NPC_SmartObject`，用于验证完全相同的行为数据；
新 C++ 模块加载后，可把实例的 AIControllerClass 换成
`AHMS_SmartObjectAIController`，验证组件化外层流程。

`BP_MVTestGameMode` 使用 `AMWTestPlayerController`，它会在 PIE 中生成
`AMVTopDownCameraActor`（约 1800 cm 臂长、-65° 俯角）并跟随玩家。
WASD 控制移动，右键点击地面寻路，左键用于瞄准测试。
