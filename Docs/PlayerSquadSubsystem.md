# 玩家小队数据与子系统

## 数据归属

`Data/Squads/SquadStructs.h` 中的 `FSquadData` 包含：

| 字段 | 用途 |
| --- | --- |
| `SquadId` | 小队唯一 `FGuid`，正式创建时分配，改名不改变身份；会议室的新队草稿在保存前没有 ID |
| `SquadName` | 小队名称 `FText` |
| `SquadIcon` | 预设队徽 `UTexture2D`，可以留空；同时用于队长头像缺失时的回退 |
| `IconSource` | `ESquadIconSource`：`Preset` 使用预设队徽，`CaptainPortrait` 使用当前队长头像 |
| `CaptainUnitId` | 队长单位 `FGuid`，必须属于本队；空队使用无效 Guid |
| `TileId` | 小队当前所在的战略瓦片 `FName` |
| `AssignedVehicle` | 兼容蓝图/存档的车辆展示及草稿快照；有效 `VehicleId` 指向玩家单位管理类中的权威记录，无效 ID 表示无车 |
| `MemberUnitIds` | 成员单位 `FGuid` 数组，按加入顺序保存 |

`UPlayerSquadManagerBase` 持有 `TMap<FGuid, FSquadData>`，以及单位、车辆各自的 `TMap<FGuid, FGuid>` 归属反向索引。成员和车辆反查均使用 ID，未绑定时查不到所属小队。`GetVehicleSquad` 提供车辆到小队的 O(1) 查询，`GetSquadVehicleShared` 返回玩家单位管理类中的同一份车辆智能指针。

**单位核心数据仍只在 `UPlayerUnitManagerBase` 的 `TMap<FGuid, TSharedPtr<FUnitData>>` 中保存一份。** 小队、UI、单位 Pawn 不增加权威副本。小队按需向单位管理类获取同一个智能指针，不缓存另一个单位数组。蓝图使用 `UUnitDataReference` 桥接共享数据；该对象的 `GetSnapshot` 仍只是用于读取/传输的副本。

`FUnitData` 不重复保存 `SquadId`，避免重新加载单位快照时覆盖小队归属。通过 `GetUnitSquad(UnitId)` 查询；`UnitStructs.h` 已记录这条规则。小队快照本身可以复制，其中成员仅以 ID 记录，不包含单位核心数据。

## 四件套

位于 `Source/SilverChoir/Public/SubSystem/PlayerSquadSubSystem`，实现文件在对应 `Private` 目录：

- `PlayerSquadSubsystem`：GameInstance 生命周期，显式依赖玩家单位子系统。
- `PlayerSquadManagerBase`：小队存储、校验和修改。
- `PlayerSquadSettings`：项目设置 → SilverChoir → 玩家小队配置；可指定处理类的蓝图子类，留空自动使用原生类。`PresetSquadIcons` 配置可选队徽的软引用数组。
- `PlayerSquadLibrary`：带世界上下文的蓝图入口，以及原生 C++ 共享数据查询入口。

无需创建子系统蓝图或手动初始化即可使用。当前没有预置小队；无车小队最多四人，分配车辆后人数上限取车辆 `PassengerCapacity`，包含驾驶人员。

## 默认小队名称表

独立数据表为 `/Game/System/SubSystem/PlayerSquadSubSystem/DT_SquadNames`，行结构 `FSquadNameTemplate`，包含 `SortOrder`（排序）和 `SquadName`（小队名称）。已在 **项目设置 → SilverChoir → 玩家小队配置 → 小队名称数据表** 中引用，可替换为自己的同类型表。初始配置为阿尔法、贝塔、伽马、德尔塔等 24 个希腊字母小队名。

**获取下一个默认小队名称** `GetNextSquadName` 按 `SortOrder` 升序、同排序值按表行名排列，跳过空白/重复名称以及已被正式小队使用的名称。查询不预占名称，也不创建小队；会议室新建按钮和空名称新队草稿使用此结果，取消后仍可使用同一名称。手动输入的名字保持不变，已有小队也不会因修改模板而自动改名。

表中名称全部使用后，使用 `第X小队` 接续编号：`X` 从有效且不重复的配置名称数量加一开始，例如当前 24 个名称用完后依次为 `第25小队`、`第26小队`。已被正式小队占用的编号自动跳过，查询或取消草稿不会消耗编号；表缺失、类型错误或没有有效行时从 `第1小队` 开始。名称匹配忽略首尾空白和英文字母大小写；解散/改名释放的表内名称可再次使用。实际创建/提交接口仍校验名称非空，算法调用方可先取得推荐名再传入。

## 蓝图使用

以下为子系统的即时业务接口；调用成功即修改权威小队数据。会议室使用后文的草稿提交方式，不在每次点击时调用这些接口。

1. 先将单位数据加载到玩家单位子系统，单位需要有效的 `当前所在瓦片ID`。
2. 调用 **创建玩家小队**，传入名称、图标和瓦片 ID，保存输出 `OutSquadId`。
3. 调用 **人员加入小队**，传入 `UnitId` 和 `SquadId`，检查返回值和 `OutError`。
4. **根据单位ID获取所属小队** 返回小队快照；**根据ID获取小队** 返回指定小队快照及成员 ID。
5. **获取小队单位数据引用** 返回 `UUnitDataReference` 数组。需要持续监听时，将对象保存到蓝图变量并绑定其 `OnDataChanged`；控件销毁/换绑时解绑。

其他节点：**人员移出小队**、**设置小队名称和图标**、**解散玩家小队**、**获取玩家小队ID列表**。

入队会先确认单位、小队存在，且两者在同一非空瓦片、目标小队未满员。校验成功后才移出原队、加入新队，满员等任何失败均保持原归属。重复加入同一队不会产生重复成员；移出尚未入队的有效单位视为成功。退队和解散均不删除单位数据，也不销毁 Pawn；空队保留，需主动解散。

## 车辆与人数上限

`FSquadData::HasAssignedVehicle()` 根据 `AssignedVehicle.VehicleId` 是否有效判断有无车辆。`GetMaxMemberCount()` 在无车时返回 4，有车时返回 `max(0, PassengerCapacity)`；座位数包含驾驶席，0 座允许分配但不能保留成员。车辆实例统一存于玩家单位管理类的共享映射，小队保存兼容快照；提交和恢复时只采信输入中的车辆 ID，其余字段重新读取权威车辆。未登记车辆拒绝分配。具体座位或驾驶员绑定尚未实现。

管理类和函数库提供以下即时接口；会议室应编辑草稿，最终统一保存：

| 接口 | 行为 |
| --- | --- |
| `SetSquadVehicle(SquadId, Vehicle, OutRemoved, OutError)` | 验证有效车辆 ID、同一非空瓦片及非负载员上限，分配车辆并裁剪超员成员 |
| `ClearSquadVehicle(SquadId, OutRemoved, OutError)` | 清除车辆并恢复四人上限，同时裁剪超员成员 |
| `GetSquadMaxMemberCount(SquadId)` | 查询正式小队当前人数上限；小队不存在时返回 0 |

同一玩家小队管理类内，一辆车的 `VehicleId` 只能分配给一个小队。更换车辆、清除车辆或解散小队后，只释放车辆归属，玩家单位子系统中的车辆记录仍保留，可再次分配。**根据车辆ID获取所属小队** 查询当前归属；C++ 的 `GetSquadVehicleShared` 读取同一个权威共享记录。

玩家单位管理类的车辆批量加载、更新和共享引用修改会先协调小队快照/容量，再通知 UI。缩容按原有队长优先规则移出超员成员；直接将车辆移到其他瓦片或移除权威车辆时，原小队解除车辆分配并恢复四人上限。正常随队战略移动请调用 **设置小队当前瓦片位置**，保持小队、成员、车辆一起移动。

`TrimMembersToCapacity()` 只修改调用对象的成员 ID 数组：从末尾依次移出，优先保留现任队长，其余保留原加入顺序。容量为零时移出全部成员并清空队长。它返回被移出的 ID，不修改单位数据或全局索引。管理类的设置/清除车辆接口在副本上裁剪，再调用 `CommitSquadDraft` 原子提交；失败时正式车辆、队长、成员和归属都不变，`OutRemoved` 为空。

成员被移出后仍在原瓦片，继续保留玩家单位管理类中的同一份 `TSharedPtr<FUnitData>`。不会删除单位、销毁 Pawn 或复制出第二份单位核心数据。

## 会议室草稿的原子保存

管理类和蓝图函数库提供 **保存小队草稿** `CommitSquadDraft(Draft, OriginalSquad, bCreateNew, OutSquadId, OutError)`。`Draft` 是期望提交的小队快照，`OriginalSquad` 是开始编辑时取得的原始快照；两者只保存成员 ID，不复制单位核心数据。

新建时设置 `bCreateNew=true`，草稿的 `SquadId` 必须无效。校验及提交成功后才分配正式 ID，失败不会创建空小队或留下半成品。编辑已有队时设置 `bCreateNew=false`，原始快照必须与当前权威记录一致；公开的 `FSquadData::MatchesSnapshot` 比较全部小队字段及车辆完整反射数据，因此车辆燃料、耐久、配置等并发改变也会拒绝过期草稿。此接口不负责修改战略位置。

提交会统一验证名称、瓦片、图标来源、人数上限、车辆座位/位置/唯一占用、成员 ID、成员当前位置和队长归属；检查所有受影响的小队与单位反向索引。超容量草稿直接拒绝，调用方选择/清除车辆时应先对草稿调用 `TrimMembersToCapacity()`。成员、车辆、队名、队徽和队长全部验证成功后一次写入，同时完成从原队转出的操作与原队队长递补，再发出单位和小队通知。任何验证失败都保留原小队数据和原归属，调用方可保留草稿供用户修正。通知中的查询可以看到完整的最终状态，通知回调中的重入写入仍被拒绝。

`WBP_SquadMeetingRoom` 的 C++ 基类维护原始快照和草稿，并使用以下规则：

- **在当前瓦片新建小队** `CreateSquadAtTile` 只打开草稿，成功返回时 `OutSquadId`、`SelectedSquadId` 仍然无效，不等同于子系统的 **创建玩家小队**。
- 队名、队徽、车辆、成员和队长操作只更新界面草稿；选择异队成员不会立即改变其权威归属。车辆变化导致的超员移出也在保存成功后才正式生效。
- **保存小队** `SaveSelectedSquad` 调用 `CommitSquadDraft`。成功后先启动返回小队列表的现有动画，再触发 `OnSquadSaved(SquadId)`；依赖新小队 ID 的业务应使用该事件参数，返回完成后编辑选择会清空。失败保留当前草稿供修正。
- 返回名册、退出 UI、重新加载列表或改选其他小队时，未保存的草稿被丢弃，不触发权威修改，也不写入存档。
- `GetSelectedSquad` 在会议室返回编辑草稿；查询管理类则返回正式数据，两者在保存前可以不同。

会议室人员行和成员卡仍绑定玩家单位管理类中的同一 `TSharedPtr<FUnitData>`。刷新按 ID 复用可见控件，保留滚动位置；成员卡绑定同一单位时不会重绑和取消待触发点击。新队徽选择弹窗、队长头像预览、底部保存入口及界面节点的具体配置见 `Docs/SquadMeetingRoomUI.md`。

## 队长和队徽

非空小队始终有一名队长。第一个加入空队的成员自动担任队长；后续加入不会取代现任队长。**设置小队队长** 只接受当前小队已有成员，不会暗中转队，也不允许将非空小队的队长清空。

队长退队或转入其他小队后，原队按照 `MemberUnitIds` 中剩余成员的加入顺序选第一人递补；原队没有成员时清空 `CaptainUnitId`。进入空队的成员自动成为新队队长，进入已有成员的小队则保留目标队现任队长。失败的转队不会改变任一小队的队长。

图标相关蓝图节点：

| 节点 | C++ 接口 | 用法 |
| --- | --- | --- |
| 设置小队队长 | `SetSquadCaptain` | 传入 `SquadId`、本队成员 `UnitId`，检查返回值和 `OutError` |
| 设置小队图标来源 | `SetSquadIconSource` | 选择 `Preset` 或 `CaptainPortrait`；空队也可预先选择头像模式 |
| 获取小队显示图标 | `GetSquadIcon` | 实时取得最终显示纹理，UI 应使用此节点而非直接读取 `SquadIcon` |
| 获取预设小队图标 | `GetPresetSquadIcons` | 按设置顺序加载可用队徽，跳过无效路径并去重；队长头像不混入此数组 |

使用即时接口选择预设队徽时，先用 **设置小队名称和图标** 更新 `SquadIcon`，再用 **设置小队图标来源** 选 `Preset`。原有名称和图标节点不会自动改 `IconSource`，因此仅编辑队名时能够保持当前图标模式。会议室弹窗在草稿中同时设置这两项，保存时统一提交，不需要串联即时修改节点。

选择队长头像时只设置 `IconSource = CaptainPortrait`，不把头像复制到 `SquadIcon`。`GetSquadIcon` 通过队长 ID 读取玩家单位管理类中同一个 `TSharedPtr<FUnitData>` 的 `Profile.PortraitTexture`；更换队长或修改其头像后，显示图标随之变化。没有有效队长或队长没有头像时返回原来的 `SquadIcon`；连预设图标也没有时返回空纹理，由 UI 显示占位样式。

`GetPresetSquadIcons` 会同步加载配置资源，宜在打开图标选择器或构建界面时调用并保存结果，不要每帧调用。它只提供预设队徽，队长头像作为独立动态选项显示。

## 战略位置与战术移动

**设置小队当前瓦片位置** 同步修改小队、所有成员及已分配车辆的 `RuntimeData.TileId`，全部写入完成后才发送通知。这样第一个收到通知的 UI 就能查询到完整的新位置；未随队的超员移出成员保留原瓦片。

这是位置提交接口，不承担 GridStrategyMapSystem 的寻路、行程时间、通行规则、`bCannotWalkStrategically` 检查或 Pawn 世界坐标移动。未来战略移动流程可在到达目标瓦片后调用它。战术地图控制单个 Pawn 时，不需要修改战略瓦片。

已入队单位的战略位置应统一通过这个接口修改。原有共享数据编辑和单位载入接口仍允许直接赋值 `TileId`，不会自动退队、移动其他队员或修正小队位置；普通业务不要单独改队员战略位置。恢复存档时先加载单位和车辆，再加载与之对应的小队。

## 通知与 C++ 使用

管理类提供以下蓝图可绑定事件：

- `OnSquadChanged(SquadId)`：小队创建、信息/位置/车辆/成员/队长/图标来源修改、解散或批量恢复；成员的共享单位数据修改也会转发此事件，让队长头像和成员卡片及时刷新。解散后查询返回 false。
- `OnUnitSquadChanged(UnitId, PreviousSquadId, CurrentSquadId)`：归属改变；未编队一侧用无效 Guid 表示。
- `OnVehicleSquadChanged(VehicleId, PreviousSquadId, CurrentSquadId)`：车辆分配、解除、改队或读档恢复后的归属变化；未分配一侧用无效 Guid 表示。

入队、退队、解散和小队改名也通知相关单位原有的 `FUnitData::OnDataChanged`。人员整备室的“所属小队”已接入真实查询，跟随该通知刷新。

小队管理类在初始化时订阅玩家单位管理类的 `OnUnitDataChanged`，在 `Shutdown`/`BeginDestroy` 中解绑。小队自身事务产生的单位通知不会被重复转发，而是在完整提交成员和队长状态后统一发出小队通知；外部修改成员数据时正常转发。UI 订阅管理类事件后仍应在销毁或换绑时解绑。

```cpp
const TArray<TSharedPtr<FUnitData>> Members =
    UPlayerSquadLibrary::GetSquadUnitsShared(WorldContext, SquadId);
for (const TSharedPtr<FUnitData>& Unit : Members)
{
    // 修改的是玩家单位管理类中的同一份核心数据。
    Unit->Modify([](FUnitData& Data)
    {
        Data.RuntimeData.CurrentStamina = 80.f;
    });
}
```

持续订阅沿用 `AddUObject + FDelegateHandle`；消费者在销毁/换绑时移除绑定并释放共享指针。仅在游戏线程访问。通知回调中只查询数据，后续修改延迟至通知完成后；小队写接口会拒绝更新过程中的重入。

## 存档

**获取玩家小队数据快照** 返回全部小队，交由现有存档流程保存。`SaveGame` 标记本身不自动写文件。

会议室尚未提交的草稿不在管理类中，因此不包含在该快照中。界面上的 **保存小队** 是将草稿提交到运行时权威数据，不负责将整个游戏存档写入磁盘。

恢复顺序：**加载玩家单位数据数组 → 加载玩家车辆数据数组 → 加载玩家小队数据**。后者全量替换现有小队并重建反向索引；输入空数组清空全部小队归属，但保留单位。恢复时先完整验证原始输入：ID 有效且唯一、所有成员存在且不重复、成员位置一致，以及已选车辆座位非负、位置一致、整批车辆 ID 不重复。即使非法成员位于将被裁剪的尾部，也会拒绝整批。完整验证通过后再按容量裁剪历史超员名单，优先保留队长，重建最终归属索引。所有原输入成员（包括被裁剪者）和此前受影响成员都参与单位通知；归属变化时再发出 `OnUnitSquadChanged`。任何错误都保留原小队数据，不删除任何单位。

旧存档没有 `CaptainUnitId` 时，非空队先选首位成员再执行容量裁剪，空队保留无效队长 ID；旧存档没有图标来源时默认 `Preset`，没有 `AssignedVehicle` 时按无车四人上限恢复。如果输入明确指定了有效的队长 ID，但该 ID 不在原始本队成员列表中，则拒绝整批，而非悄悄替换队长；非法 `IconSource` 枚举值同样拒绝整批。显式保存的合法队长在容量大于零时优先保留，图标模式原样恢复。

## 验证入口

2026-09-28 编号回退调整：名称表耗尽后改为“第X小队”，两项 `SilverChoir.Player.Squads.Names` 测试通过；24 个有效配置名之后从“第25小队”继续，空表从“第1小队”开始。日志 `Saved/Logs/GameTimeSystemEditorTests.log`。

2026-09-28 默认名称表改造：完整编译通过，14 项核心数据测试通过，日志 `Saved/Logs/SquadTransferNamesCoreTests.log`。新增 `SilverChoir.Player.Squads.Names.TemplateOrderAndReuse` 与 `SilverChoir.Player.Squads.Names.Fallback`；转队确认、保存语义及两种窗口比例的运行验证见 [SquadMeetingRoomUI.md](SquadMeetingRoomUI.md) 的“转队确认与默认名称表验证”。

2026-09-28 容量与车辆分配改造：完整编译通过，日志 `Saved/Logs/SquadVehicleCapacityBuild.log`。十一项编辑器数据测试均通过，日志 `Saved/Logs/SquadVehicleCapacityCoreTests.log`，包含三项容量测试 `Capacity.ValueRules` / `Capacity.AssignmentAndTransfers` / `Capacity.ValidationAndSaveGame`、四项草稿测试、三项原小队数据测试及单位唯一共享数据回归。实际游戏世界中的 `Subsystem` 函数库测试也通过，日志 `Saved/Logs/SquadVehicleCapacityRuntimeTests.log`。

- `SilverChoir.Player.Squads.Membership`：入退队、失败转队保留原队、智能指针身份、位置同步和通知重入。
- `SilverChoir.Player.Squads.Persistence`：原子恢复、成员唯一性、图标 GC、清空与关闭。
- `SilverChoir.Player.Squads.CaptainAndIcons`：首任队长、提升与自动递补、失败转队保留队长、动态头像/预设回退、共享数据身份、旧存档迁移、非法队长和图标类型拒绝、重入及解绑。
- `SilverChoir.Player.Squads.Subsystem`：实际游戏世界依赖初始化、函数库、蓝图共享引用。
- `SilverChoir.Player.SharedUnitData`：既有单位唯一存储、通知与 GC 回归测试。
- `SilverChoir.BaseUI.SquadMeetingRoom.DataAndControls`：真实会议室蓝图挂载、瓦片筛选、人员共享引用、成员卡片与队长操作、队名/图标交互及生命周期解绑。
- `SilverChoir.BaseUI.SquadMeetingRoom.TransitionsAndCapture`：实际 Tick 下的层级动画、重入拒绝及动画卸载；截图写入 `Saved/Screenshots/SquadUI`，可用 `-SquadCaptureTag=...` 区分窗口比例。
- `SilverChoir.BaseUI.SquadMeetingRoom.InterruptedTransitions`：动画中外部解散选中小队、返回途中关闭、入场途中关闭。

前三项及 `SharedUnitData` 使用 EditorContext；`Subsystem` 和三项会议室测试使用 ClientContext，在独立 `-game` 进程运行。会议室测试只在该进程内创建数据，不保存蓝图或玩家存档。

既有验证记录（草稿改造前）：2026-09-27，完整编译成功，上述八组测试均返回 Success。编辑器日志 `Saved/Logs/SquadCoreTests.log`，游戏进程日志 `Saved/Logs/SquadUIRuntimeTests.log`。这些结果不代表此次 `CommitSquadDraft`、界面草稿取消/提交或新队徽弹窗已完成验证。UI 接入与历史分辨率验证详见 `Docs/SquadMeetingRoomUI.md`。

初版验证记录（不代表本轮新增测试结果）：2026-09-27，SilverChoirEditor Win64 Development 完整编译成功，`Membership`、`Persistence`、`Subsystem`、`SharedUnitData` 四组测试均返回 Success。
编辑器测试日志：`Saved/Logs/PlayerSquadEditorTests.log`；游戏世界测试日志：`Saved/Logs/PlayerSquadRuntimeTests.log`。
运行时测试启动阶段仍有引擎 ToolsetRegistry PythonTestRunner 的既有初始化错误，未计入本次测试，测试队列正常完成。
