# 掩体智能对象 / Cover Smart Objects

面向 SilverChoir / UE 5.8 的独立 C++ 插件。保留 AI Cover System 的“导航网格边界采样 → 几何遮挡和探身检测”路线，使用引擎公共 API 重新实现；不需要启用商城 AICoverSystem，也不复制其源码或资源。

## 使用

1. 编译后重新打开编辑器，在插件列表确认 **掩体智能对象 (Cover Smart Objects)** 已启用。
2. 地图需要已构建的 Recast NavMesh。放置 **Cover Smart Object Volume**，调整 `Generation Bounds / Box Extent` 覆盖区域。保持 Actor/组件缩放为 1、无俯仰和翻滚。
3. 设置 `Agent Profile`，点击 **Bake Cover**，检查 `Last Bake Report`，保存地图。World Partition 地图需要先加载待烘焙单元；建议每个流送区域各放一个小 Volume。
4. 开启 Volume 的 `Draw Debug` 检查烘焙点和探出方向。青色为蹲姿，黄色为站姿，绿色箭头为静态可探出方向，红色表示烘焙数据已失效。
5. 游戏中获取 `CSOCoverSubsystem`（Get World Subsystem），构造 `CSOCoverQuery`，调用 **Find And Claim Cover** 或 **Find Cover Queued**。只查看结果可调用 **Find Cover**。
6. `Status == Success` 后移动到 `Location`，按 `WallDirection` 朝向掩体。`bCrouched` 指定躲藏姿态，`Peek` / `PeekLocation` 给出这次查询能够看见目标的探头方式和世界眼睛位置。
7. 抵达后先 **Validate Cover**，再 **Occupy Cover**。持续使用时周期调用 **Renew Cover Lease**；离开、任务终止或死亡时调用 **Release Cover**。离开视野或场景变化后，射击前再次验证。

输入 `Origin` 是角色脚底；`TargetEnemy`、`Enemies` 默认也是脚底，统一加 `EnemyEyeHeight = 160cm`。若传入真实眼睛/瞄准坐标，将 `Enemy Positions Are Eyes` 打开。目标不需要重复放入数组；重复坐标会去重。占用和寻路时将 `User` 设置为查询 Pawn 或 Controller。

## 筛选规则

- 硬条件：角色胶囊有空间；目标看不到头顶、胸、双肩、骨盆和膝部六个采样点；至少一种烘焙允许的探身方式当前能看见目标。默认还要求存在完整导航路径。
- 半墙起身：重新检测站立胶囊、从蹲姿眼睛到站姿眼睛的球形扫掠、站姿眼睛到目标的射线。
- 全墙侧探：按你的确认，采用 **越过墙角后再露出 20cm**。烘焙分别测量左右边缘，将到边缘的横向距离加上 `LeanDistance = 20cm`，保存为每点左右实际探头距离。运行时验证到该位置的扫掠及对目标的射线。探头搜索有最大距离限制，过宽墙体中段不会生成不合理的超长探身。
- 其他敌人：每人只要看得到任何身体采样点，即计为暴露；全部采样点被挡住才算对该敌人隐蔽。
- `Balanced` 默认分数 = 距离厘米 + 暴露敌人数 × `ExposurePenalty`（默认 1000cm），越小越好。
- `SafestThenNearest` 先比较暴露人数，再比较距离；`Nearest` 只比较满足硬条件后的距离。
- 距离为三维直线距离，导航用来检验可达性；分数不是导航路径长度。

`Enemies` 最多 32 项，`IgnoredActors` 最多 64 项，过多返回 `InvalidRequest`，不会静默忽略敌人。Pawn 不作为遮挡墙，位置接口不要求另行提供敌人 Actor；使用碰撞过滤和受预算约束的命中重试，不扫描全场 Pawn。`IgnoredActors` 用于额外排除射线命中的附属物等；角色占位胶囊仍会阻止进入已站人的位置。视线使用 `TraceChannel`（默认 Visibility），身体空间和探头轨迹使用独立的 `MovementChannel`（默认 Pawn）；烘焙也有对应的两个通道。查询的 `Trace Complex` 应与烘焙视线设置保持一致。

## 数据与性能

每个 Volume 序列化紧凑的局部位置、墙面朝向、躲藏姿态、探身位掩码和左右实际探头距离。角色尺寸和烘焙条件也被保存，用于拒绝过期数据。运行时为每点创建一个真正的 Smart Object 实例和单槽位，共享默认 Definition；不创建逐点 Actor 或 Component，也不逐点 Tick。

`UCSOCoverSubsystem` 持有 10 米立方体的三维哈希分区（包含楼层和负坐标），只查询相关单元。候选由近及远排序，再执行昂贵碰撞检测，最后按战术排序做有限次数的寻路。占用直接由 UE Smart Object 槽位管理；流送卸载时销毁相应实例，句柄失效，后续加载使用新 ID。

默认每次最多 128 个候选、2048 次碰撞检测、16 次寻路。碰撞预算包含射线、扫掠和重叠检测。单次查询还有内部单元/记录访问上限。`bSearchTruncated` 表示检索因预算未完全覆盖搜索空间：成功结果满足验证条件，但不能声称是全范围最佳；无已验证结果时返回 `BudgetExceeded`，不能当作 `NoCover`。

大量 AI 使用 `Find Cover Queued`：FIFO 最多 128 项；默认每帧最多处理两个完整查询，并在查询之间检查 2ms 时间预算。可用 `cso.AsyncQueriesPerFrame`、`cso.AsyncMilliseconds` 调整。这是游戏线程分帧调度，单个物理/寻路调用不会中途抢占，因此不是严格的毫秒截止保证。排队期间输入是快照；调用者应提交最新敌人坐标，并在实际使用前重新验证。

不缓存敌人的可见性结果；缓存的是静态几何能力。这样移动敌人、门或障碍发生变化后，不会因为旧的“可隐蔽”缓存直接接受候选。墙被拆除后可调用 `Invalidate Covers In Bounds` 主动移除相关点；**不会自动为新几何生成新点**。

## 调试

控制台命令：

```text
cso.Debug 1
cso.DebugRadius 5000
cso.DebugMaxPoints 256
cso.DebugPartitions 1
cso.DebugLabels 1
cso.DebugQuery 1
```

前五项显示附近烘焙能力、分区、ID、占用及租约；最后一项显示当前查询遮挡射线、射击射线和拒绝原因。也可只打开单次 Query 的 `Draw Debug`。静态绿色探身箭头只说明烘焙时前方可用，实际能否命中某个敌人仍以当前查询为准。关闭时将对应开关设为 0。

结果带候选、碰撞和寻路次数；子系统提供已注册掩体/分区/预约数量。Unreal Insights 中可查看 `CSO_FindCover` 与 `CSO_ValidateCover` 的 CPU 区间。

## 集成边界与验收

- 这是可迭代的首版，不是已经完成真实大地图性能验收的商业成品。需要按目标硬件、地图碰撞和 AI 密度测量 P95/P99 延迟，再设定预算。
- 当前为编辑器烘焙，使用已加载区域的默认 Recast NavMesh。运行时增量 NavMesh 烘焙、自动新掩体生成、移动掩体跟随、多尺寸 NavData 选择尚未实现。
- 一个 Volume 的 Agent Profile 代表一类角色体型。传入 Pawn/Controller 时会检查导航代理尺寸并排除过小的掩体配置；只传坐标时仍由调用者保证角色体型适合。具体眼高、枪口、动画姿态需游戏层统一配置。
- 智能对象和占用运行在服务器/单机权威端；不会自动把调试数据、槽位状态或动画复制到客户端。
- 眼睛射线符合当前需求，但不等同于枪口弹道。动画、身体侧倾、枪口偏移、武器长度、弹丸半径和到达姿态仍由游戏战斗层负责；射击时应做实际枪口检测。
- 查询只保证采样时、指定碰撞通道下的遮挡和瞄准条件；六点采样不是连续身体表面的数学保证，也没有模拟敌人未来移动。
- 现有 HMS 的交互任务要求 SmartObject Actor/Component。当前采用无逐点 Actor 的原生实例，不能直接传给 HMS 座椅等现有任务。接口已返回原生对象/槽位/占用句柄，后续需独立的掩体移动与动画适配，避免重复 Claim。
- 自定义 Smart Object Definition 必须有效、只有一个槽位、无用户标签过滤及前置条件，且槽位偏移和旋转为零。默认无需创建资产。

建议在真实地图验收：半墙、窄柱、左右墙角、斜坡、楼层、窗洞、低天花板、门开关、敌人绕侧、路径被堵、抢占竞争、角色销毁、分区卸载、世界重载，以及 100/500 AI 同时发起请求。验证记录见 [Docs/Validation.md](Docs/Validation.md)。

引擎接口依据：[Smart Objects 概览](https://dev.epicgames.com/documentation/unreal-engine/smart-objects-in-unreal-engine---overview?lang=en-US)、[USmartObjectSubsystem](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/SmartObjectsModule/USmartObjectSubsystem?lang=en-US)，以本机 UE 5.8.3 源码签名为准。
