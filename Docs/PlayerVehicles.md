# 玩家车辆数据与小队选车

车辆数据统一存放在玩家单位子系统的 `UPlayerUnitManagerBase` 中。每个 `VehicleId` 对应唯一的 `TSharedPtr<FVehicleData>`；人员与车辆使用各自的 ID 映射，不混用记录。

## 蓝图初始化

在新游戏初始化流程中调用玩家单位蓝图函数库的 **从模板表加载玩家车辆**：

- `Table`：车辆模板表，行结构为 `FVehicleTemplate`。留空时读取项目设置「SilverChoir → 玩家单位配置 → 车辆模板数据表」。当前配置为 `/Game/System/Data/Vehicles/DT_VehicleTemplates`。
- `TileId`：这些车辆最初所在的战略瓦片，必须填写。
- `OutVehicleIds`：新创建的车辆 ID。
- 返回值和 `OutError`：判断是否成功并获取错误原因。

这个节点按行名字排序读取整张表，每行创建一辆车并加载到管理类。现有表包含 2 座跑车、4 座轿车和 6 座车；座位数包含驾驶席。子系统初始化时不会自行执行该节点。

**每次显式调用都会创建新的车辆 ID 并追加车辆。** 不要在打开会议室、每帧更新或读取存档时重复调用。恢复存档使用 **加载玩家车辆数据数组**：同 ID 原地更新，未包含的车辆保留；无效 ID、重复 ID 或负座位数使整个批次失败，不写入部分结果。存档中的车辆允许位置为 `None`，但不会出现在当前瓦片查询中。

## 小队会议室

进入会议室时，原有 **展示小队列表(TileId)** 同时设置车辆列表使用的瓦片。点击「选择车辆」会读取该瓦片的玩家车辆，并显示 16:9 预览图、名称、座位数与占用状态。

若只需单独刷新车辆来源，可对会议室控件调用 **加载当前瓦片车辆列表(TileId)**。异地车辆不显示；已分配给其他小队的同地车辆显示禁用。车辆变化后界面重新读取共享数据，不需要重新创建车辆。

选择车辆与「清除车辆」均只修改会议室草稿，点击 **保存小队** 才提交分配、队名、队徽、队长与成员，并返回小队列表。直接返回或关闭界面会放弃未保存草稿。

- 未分配车辆时容量为 4 人。
- 分配车辆时容量等于总座位数。
- 缩容优先保留队长，其余按原加入顺序保留；超出人员移出草稿，保存前会显示提示。移出小队不会删除人员的核心数据。
- 同一辆车只能正式分配给一个小队；草稿不会预占车辆，保存时再次检查占用、位置及权威数据。

旧节点 **设置可选车辆列表** 仅保留为蓝图兼容入口，已标记弃用。它不会导入车辆或替换玩家车辆集合；新流程使用上面的加载和当前瓦片查询节点。

## 查询、修改与存档

| 蓝图节点 | 用途 |
| --- | --- |
| 获取玩家车辆ID列表 | 获取全部车辆的 ID |
| 根据ID获取车辆数据快照 | 获取读取或存档用的副本 |
| 获取当前瓦片车辆数据 | 精确匹配 `TileId`；`None` 返回空数组 |
| 更新玩家车辆数据 | 将同 ID 的修改提交到权威记录 |
| 移除玩家车辆数据 | 删除记录并解除小队分配 |
| 根据车辆ID获取所属小队 | 查询已保存的正式归属 |

蓝图快照不是共享引用；修改快照后要调用更新节点才会生效。保存时可按 ID 枚举并取快照。读档顺序为 **单位数据 → 车辆数据 → 小队数据**，以便小队中的人员和车辆 ID 都能够解析。

小队的 `AssignedVehicle` 保留为旧蓝图和存档兼容的展示、草稿快照。它的 `VehicleId` 才是关联键；小队提交时重新读取权威车辆，不把草稿中的车辆字段写回管理类。

## C++ 共享数据规则

通过 `UPlayerUnitLibrary::GetVehicleDataShared(WorldContext, VehicleId)` 或管理类同名方法取得原始共享指针。已分配的小队也可通过 `UPlayerSquadLibrary::GetSquadVehicleShared` 查询。

```cpp
TSharedPtr<FVehicleData> Vehicle =
    UPlayerUnitLibrary::GetVehicleDataShared(this, VehicleId);
if (Vehicle)
{
    Vehicle->Modify([](FVehicleData& Data)
    {
        Data.RuntimeData.CurrentFuel = FMath::Max(0.f, Data.RuntimeData.CurrentFuel - 5.f);
    });
}
```

使用 `OnDataChanged.AddUObject` 和 `FDelegateHandle` 订阅，消费者换绑或销毁时移除句柄并释放指针。修改应通过 `Modify`；直接修改字段后必须调用 `NotifyDataChanged`。不要改 `VehicleId`，不要在通知回调中继续修改玩家数据，需要继续操作时延迟到后续帧。

通知前管理类先协调小队的车辆关联与容量，然后通知消费者；整轮通知期间拒绝重入写入。同 ID 重新加载保持地址和订阅；复制结构体不复制事件。图片、实体类等 UObject 引用由共享分配的 GC 桥接追踪，外部共享指针仍持有时资源不会被提前回收。

小队在战略地图上整体移动时使用小队管理的 `SetSquadTileId`，使人员和车辆同步移动。直接把已分配车辆移到其他瓦片或移除车辆，会解除原小队的车辆分配并重新应用默认容量。已删除记录的旧共享指针可能因外部持有而仍存在，但不再是玩家拥有的车辆；收到删除通知后应按 ID 重新查询并解除绑定。

## 回归测试

测试源码：`Source/SilverChoir/Private/Tests/PlayerVehicleSubsystemTests.cpp`。

- `SilverChoir.Player.Vehicles.SharedData`：原子校验、共享地址、通知时序与重入、快照、瓦片过滤、移除与 GC 生命周期。
- `SilverChoir.Player.Vehicles.TemplateTable`：整表排序、唯一 ID、追加创建、位置、配置回退与错误、空表。
- `SilverChoir.Player.Vehicles.Library`：运行时世界上下文中的函数库加载与清理。

前两项使用 EditorContext，第三项使用运行中的 Game world。测试是否通过以本次实际执行日志为准。

### 本轮验证（2026-09-28）

- `SilverChoirEditor Win64 Development` 完整编译和链接成功，日志 `Saved/Logs/PlayerVehicleStoreBuild.log`。
- 17 项编辑器数据测试通过，进程退出码 0，日志 `Saved/Logs/PlayerVehicleStoreCoreTests.log`。涵盖新增车辆共享存储与整表初始化、原有车辆工厂与序列化、小队车辆绑定/容量/草稿、人员共享数据回归。直接 `Modify` 的测试确认：先修复小队关联，再通知消费者，整个通知阶段拒绝修改其他车辆或重入管理类。
- 10 项实际游戏运行测试通过，进程退出码 0，日志 `Saved/Logs/PlayerVehicleStoreRuntimeTests.log`。涵盖真实会议室控件、动画、保存返回、席位容量、车辆选择器、玩家车辆函数库及小队子系统。验证异地车辆不生成行、同地占用禁选、车辆实时移入/移出列表、过期点击取消及退出解绑。
- 本轮未修改蓝图布局或动画；1920×1080 实际渲染截图为 `Saved/Screenshots/SquadUI/VehicleStore_1920x1080_01_Choices.png`、`VehicleStore_1920x1080_02_SelectedPreview.png`。测试使用临时车辆/小队，未加载基地室内子关卡，未向正式资产或存档写入测试记录。没有执行完整打包。
