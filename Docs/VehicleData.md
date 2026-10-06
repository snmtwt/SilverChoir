# 车辆数据与通用战略移动数据

## 文件组织

| 文件（均相对于 Source/SilverChoir/Public） | 职责 |
| --- | --- |
| `Data/Common/StrategicMovementStructs.h` | 单位与车辆共用的 `FStrategicMovementData` |
| `Data/Vehicles/VehicleStructs.h` | 车辆资料、属性、实体配置、运行状态、模板和实例 |
| `Data/Vehicles/VehicleDataLibrary.h` | 从模板或数据表生成车辆实例数据的算法节点 |
| `Object/Vehicle/VehiclePawnBase.h` | 可创建子蓝图的车辆 Pawn 基类 |
| `Data/Units/UnitStructs.h` | 单位模板与单位实例也包含 `StrategicMovementData` |

## 数据分层

`FVehicleTemplate` 继承 `FTableRowBase`，用于数据表中定义车型；`FVehicleData` 表示一辆实际车辆，带有独立的 `VehicleId`。同一车型生成两次会得到两个不同的 ID，默认构造结构体不会自动生成 ID。

- `Profile`：车辆名称、说明、展示图片。
- `Attributes`：最大耐久、最大燃料、座位数（包含驾驶席）、最大载重。
- `EntityData`：选择 `AVehiclePawnBase` 的子类，作为该车型的实体类。
- `StrategicMovementData`：战略瓦片之间的行军速度与基础消耗。
- `RuntimeData`：实例当前耐久、燃料和战略瓦片 ID；不放在车型模板中。

数值默认值用于初始配置，实际平衡数值在车型数据表中设置。模板修改影响后续生成的车辆，已经生成的实例保留自己的数据。

从表生成时记录 `SourceTemplateRow`，用于追溯行名；直接从模板结构体生成时为 `None`。这只是来源提示，不是车辆身份，也不持有模板表引用。

车辆基类继承 `APawn`，提供场景根组件和车辆 ID 关联。模型、碰撞及驾驶组件由车辆子蓝图选择，便于后续接入不同车辆类型。实体只持有 `VehicleId`，没有另一份可修改的 `FVehicleData`。`InitializeVehicleId` 建立关联，`GetVehicleId` 读取关联；这不是向玩家仓库登记车辆的操作。

## 蓝图使用

1. 基于 **车辆 Pawn 基类** 创建具体车辆子蓝图，添加模型等组件。
2. 创建 Data Table，行结构选择 `VehicleTemplate`，在每行中设置资料、图片、属性、实体类和移动参数。
3. 调用 **根据模板创建车辆数据**，或 **根据模板表行创建车辆数据数组**。表行数组决定要生成哪些车辆，重复填写同一行会生成多辆独立车辆；空数组代表不创建。
4. 将返回数组交给玩家单位函数库的 **加载玩家车辆数据数组**。也可直接用 **从模板表加载玩家车辆**，传入当前瓦片 ID，一步为全表每行创建一辆并登记到玩家单位管理类。需要在地图生成实体时，读取 `EntityData.VehiclePawnClass`，检查已配置且可生成，用 Spawn Actor 创建，再调用 `InitializeVehicleId` 传入返回的 `VehicleId`。

`UVehicleDataLibrary` 提供：

| C++ 方法 | 输入 | 输出 |
| --- | --- | --- |
| `CreateVehicleDataFromTemplate` | 模板、当前 `TileId` | 一条新车辆数据 |
| `CreateVehicleDataBatch` | 模板、数量、当前 `TileId` | 成功状态、数据数组、错误信息 |
| `CreateVehicleDataFromTable` | 数据表、行名数组、当前 `TileId` | 成功状态、数据数组、错误信息 |

以上都是带执行引脚的创建节点，避免纯节点反复求值时意外生成多个 ID。生成只创建结构体，不会生成 Actor、写入磁盘存档或为小队分配车辆。

## 小队座位容量

沿用 `FVehicleAttributes.PassengerCapacity` 作为总座位数，包含驾驶席，不再添加另一份容量字段。未分配车辆的小队最多 4 人；分配车辆后上限完全由其座位数决定，例如 6 座允许 6 人、2 座只允许 2 人，0 座不允许成员。选择车辆不会额外叠加默认的 4 人。

车辆权威数据统一存于 `UPlayerUnitManagerBase::VehicleDataStore`，其值为 `TSharedPtr<FVehicleData>`。`FSquadData.AssignedVehicle` 保留为兼容既有蓝图和存档的展示/草稿快照，以有效 `VehicleId` 区分有车和无车；分配与保存时按 ID 重新查询权威记录，不采信外部快照中的容量或位置。会议室从子系统读取当前瓦片车辆，异地不显示、他队占用禁选；不应每次打开界面都重新生成车辆。`SetSelectedSquadVehicle` / `ClearSelectedSquadVehicle` 修改当前草稿，更新人数上限和空位显示；`SaveSelectedSquad` 成功才将车辆和成员变更一起提交，返回或关闭而不保存会放弃。具体座位与驾驶员绑定仍未实现。

同 ID 加载原地更新，保留已有智能指针及修改事件。C++ 消费者通过 `UPlayerUnitLibrary::GetVehicleDataShared` 获取权威引用，使用 `Modify` 修改；蓝图使用快照和显式更新节点。车辆与小队的关系由小队管理类的车辆反向索引维护，移除车辆会自动解除分配。恢复存档先加载单位和车辆，再加载小队。完整节点说明见 [玩家车辆管理](PlayerVehicles.md)。

缩容时优先保留队长，其他成员按加入顺序保留；从末尾移出的人员仅解除小队归属，仍在原战略瓦片，单位共享数据和库存都不删除。清除车辆时回到 4 人上限，同样处理超员。运行时直接操作可使用玩家小队函数库的 `SetSquadVehicle` / `ClearSquadVehicle`，成功后返回移出的单位 ID。车辆必须与小队同处一个有效瓦片，且不能同时分配给其他小队。

创建时填满耐久和燃料，并将所有实例放到传入的战略瓦片。负值及非有限浮点数修正为零，源模板不变。批量数量为零时成功返回空数组，负数返回错误；数据表无效、行结构不匹配或任何一行不存在时，清空输出并整体失败。图片和实体类允许暂未配置，便于先制作车型数据；实际生成 Actor 时再检查实体类。

## 战略移动参数

`FStrategicMovementData` 表示战略地图中从一个瓦片移动到另一个瓦片所需的基础能力与消耗，单位和车辆共用同一类型。

| 字段 | 含义 | 单位 | 占位默认值 |
| --- | --- | --- | --- |
| `SpeedTilesPerHour` | 基础战略移动速度；0 表示不能行军 | 瓦片/游戏小时 | 1 |
| `StaminaCostPerTile` | 每移动一瓦片的基础体力消耗 | 体力/瓦片 | 0 |
| `FuelCostPerTile` | 每移动一瓦片的基础燃料消耗 | 燃料量/瓦片 | 0 |

这些默认值只供初始配置，尚未确定实际平衡规则。步行/公路/飞行等模式属于小队出行策略，不放进每个单位或车型模板。人员通常使用体力消耗，车辆通常使用燃料消耗，具体结算仍由出行业务选择适用字段。

GridStrategyMapSystem 的 `UGSMNavigationMoveData` 保存路线瓦片 ID、瓦片对象和连接类型；`FGSMTileNavigationSettings` 提供步行耗时/体力、公路耗时、车辆耗时/燃料、飞行耗时等倍率，供后续按路线及出行方式结算。插件中的 `WalkEnterCost`、`RoadEnterCost` 和连接 `CostOverride` 是寻路代价，不是游戏小时；也不能把路径 `TotalCost` 直接当作移动时间。

普通相邻瓦片的一步按一个标准瓦片计算，长距离公路连接的距离由行军业务确定。基础耗时为 `标准瓦片距离 / SpeedTilesPerHour × 耗时倍率`，资源消耗为 `标准瓦片距离 × 每瓦片消耗 × 消耗倍率`。速度为零时应禁止出发，不参与除法。直接修改数据或读取外部存档后，结算入口仍需校验参数；`GetSanitized()` 返回修正副本，模板工厂已调用它。人员步行还需检查既有的 `RuntimeData.bCannotWalkStrategically`。

当前只定义数据及模板创建逻辑，尚未实现行军耗时、资源扣除、时间推进、路线执行或到达后的 `TileId` 更新。没有把这些字段连接到 Pawn 的速度、加速度、转向或车辆驾驶组件。

`FUnitTemplate` 与 `FUnitData` 都包含同类型字段。现有模板创建入口继承并清洗这些参数；负数、NaN、无穷值归零，源模板不变。单位仍只由玩家单位管理类保存唯一权威实例，实体和 UI 通过原来的共享指针读取。修改应调用 `Data->Modify(...)`，让订阅者收到现有的数据修改事件；加载同 ID、更新和快照均保留移动参数，不另外建立单位数据缓存。

### 旧字段处理

此前误加的 `TacticalMovementData` 使用 cm/s 等战术实体单位，不能直接换算为战略瓦片速度。新字段 `StrategicMovementData` 使用以上默认值，不迁移旧战术数值；已有模板需要按战略规则重新填写。不要把旧 `MaxSpeed`、加速度、转向等值写入新的三个参数。

`DefaultEngine.ini` 已配置结构类型和单位/车辆四处外层属性的 Core Redirects。若蓝图曾拆分旧结构体引脚或使用旧 Make/Break 节点，需要刷新这些节点并重新配置新参数；旧加速度、转向等引脚没有对应的战略含义。

## 验证

- 2026-09-28 小队座位容量接入：完整编译成功，三个容量/分配/SaveGame 测试及其余小队数据回归通过（共十一项编辑器测试），日志 `Saved/Logs/SquadVehicleCapacityBuild.log`、`Saved/Logs/SquadVehicleCapacityCoreTests.log`。车辆草稿与空位卡在实际会议室蓝图中的运行测试通过，详见 `Docs/SquadMeetingRoomUI.md`。

- 2026-09-28，`SilverChoirEditor Win64 Development` 完整编译及链接成功：`Saved/Logs/StrategicMovementBuild.log`。使用 Visual Studio 相同目标及参数，并用 `-gather` 重新收集改名后的文件。
- 八项数据测试全部返回 `Success`：三个 `SilverChoir.Data.StrategicMovement` 测试、三个 `SilverChoir.Vehicle.Data` 测试，以及 `SilverChoir.Player.SharedUnitData`、`SilverChoir.Player.UnitTemplateData`。日志：`Saved/Logs/StrategicMovementTests.log`。
- 覆盖战略参数默认值与非法值处理、模板继承、唯一 ID、重复表行、缺行时无部分输出、资源/类引用、SaveGame 归档往返、共享指针地址和修改通知，以及结构/属性重定向、旧数值字段不映射。
- 启动阶段仍记录既有的 `Condition failed` 和 EasyHouseBuilderEditor `SetWallOpenings is not AICallable` 日志；与旧验证日志相同，发生在本次八项测试之前，测试队列正常完成。
