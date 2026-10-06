# 玩家单位管理子系统

目录：`Source/SilverChoir/Public/SubSystem/PlayerUnitSubSystem`，对应实现位于 Private 同路径。

- `PlayerUnitSubsystem`：GameInstance 子系统，创建、持有和释放处理类，提供就绪状态与初始化错误。
- `PlayerUnitLibrary`：带世界上下文的蓝图函数库，提供注册、移除、ID 查询、枚举和清空登记。
- `PlayerUnitSettings`：项目设置 → SilverChoir → 玩家单位配置；ManagerClass 留空使用原生处理类，也可指定其蓝图子类。
- `PlayerUnitManagerBase`：业务处理层，可作为蓝图父类扩展单位逻辑，支持自动世界上下文。

基础登记采用非空 FName ID 和当前世界中的 Actor 弱引用。一名 Actor 对应一个 ID，重复 ID 不覆盖已有有效单位。
销毁或切换地图后不返回旧 Actor。移除/清空登记不会销毁 Actor，也不负责生成单位或保存永久人员数据。
蓝图节点位于“玩家单位”分类；未就绪/无世界上下文时查询返回空、操作返回 false。

验证：`SilverChoir.Player.UnitSubsystem`。

## 单位数据

`Data/Units/UnitStructs.h` 定义 FUnitProfile（档案）、FUnitAttributes（基础属性）、
FUnitRuntimeData（当前状态）、FUnitData（含唯一 FGuid 的实例数据）和 FUnitTemplate（DataTable 行）。
原文件中重复命名的属性结构已修正为 FUnitAttributes。
力量、敏捷、灵巧、智力、体质默认 10，枪法、爆破、黑客、医疗默认 0；负重上限默认 30 kg。
这些数值只是模板起始占位，不是已确定的平衡规则；属性不硬编码最大等级或命中率公式。

蓝图节点“根据模板创建单位数据”复制档案、属性、实体配置和战略移动数据，生成新 UnitId，按属性上限初始化血量与体力。
默认构造 FUnitData 不生成 ID，复制已有数据保持 ID；负数属性及无效浮点上限在创建入口归零。
该节点不生成/登记 Actor，也不初始化背包物品。背包物品仍由 StrategyInventorySystem 负责。
持久数据字段及嵌套成员带 SaveGame 标记，但实际存档读写仍需业务层实现。
Actor 注册表仍使用 FName，FUnitData 使用 FGuid；二者独立。

### 通用战略移动参数

`FUnitTemplate.StrategicMovementData` 和 `FUnitData.StrategicMovementData` 使用 `Data/Common/StrategicMovementStructs.h` 中的 `FStrategicMovementData`，与车辆共用同一类型。该结构描述战略瓦片之间的移动，包含：

- `SpeedTilesPerHour`：基础速度，单位为瓦片/游戏小时，默认 1；0 表示不能行军。
- `StaminaCostPerTile`：每瓦片基础体力消耗，默认 0。
- `FuelCostPerTile`：每瓦片基础燃料消耗，默认 0，供车辆等出行业务使用。

以上均为占位数值，未确定平衡规则。模板生成时复制并将负数或非有限值归零，源模板不变。出行模式属于小队策略；本结构不保存路线、当前位置、移动进度或实际乘员关系。

该字段作为单位权威数据的一部分被保存、复制和原地更新；已有共享指针读取到修改后的值，使用 `Modify` 或 `NotifyDataChanged` 通知。当前只提供数据，没有接入实际行军、资源结算或时间推进，设置字段也不会自动改变战略 `TileId` 或现有 Mover 组件。

GridStrategyMapSystem 的导航移动对象提供路径和连接类型，瓦片导航设置中的步行/公路/车辆/飞行耗时及体力、燃料倍率供后续结算使用。插件寻路的进入代价和路径 `TotalCost` 用于路径选择，不等同于游戏时间；实际移动时间须结合战略速度、路线及对应倍率另行结算。车辆数据的完整分层与创建节点见 `Docs/VehicleData.md`。

此前误加的 `TacticalMovementData` 是 cm/s 等战术实体参数，不能直接换算为战略瓦片速度。新 `StrategicMovementData` 从占位默认值开始，不继承旧战术参数数值；已有模板需要按战略规则重新配置。

本轮战略移动字段修正的编译和回归测试待验证，完成后补充日志；先前战术参数版本的通过记录不作为本轮验证结论。

## 模板表与集中存储

战略位置：FUnitRuntimeData.TileId 是带 SaveGame 标记的 FName，使用 GridStrategyMapSystem 的格子标识。它仅记录位置，不自动移动棋子或校验地图格子是否存在。None 表示未分配。

“根据模板表行创建单位数据数组”新增 TileId 输入，所有新单位的 RuntimeData.TileId 都设置为该值。已有蓝图调用需要在新引脚填写位置，留空仍为 None。

人员整备室左侧：在界面创建后调用“加载人员列表”，输入当前 TileId。待命仅显示相同且非 None 的位置，全部显示子系统所有单位，但异地单位禁用、不可选择。界面不再自动生成测试人员。列表读取共享数据并生成临时展示字段，监听 OnUnitDataChanged 后刷新；选择不会更新右侧详情。重建筛选列表时先构建并测量行控件，再刷新滚动容器布局，避免首次绘制高度尚未计算。“获取选中单位ID”返回真实 FGuid，可用于后续业务。原 SelectedPersonnelID 使用 GUID 的字符串形式兼容旧行控件。切换右侧页签时原列表/仓库滑动逻辑保持不变。

默认模板表：`/Game/System/SubSystem/PlayerUnitSubSystem/DT_UnitTemplates`，行类型 FUnitTemplate，初始为空表，可自行添加人员模板。项目设置中的 UnitTemplateTable 可替换默认表。

蓝图流程：
1. 使用“根据模板批量创建单位数据”（模板 + 数量）、“根据模板数组创建单位数据”，或“根据模板表行创建单位数据数组”（表 + 行名数组）。表引脚为空时使用配置表；重复行名生成不同 ID 的单位；缺失行使整批失败。
2. 调用“加载玩家单位数据数组”。按 UnitId 合并：新 ID 新增，已有 ID 原地更新，未传入的单位保留。无效或重复 ID 整批拒绝，错误通过 OutError 返回。
3. 其他蓝图保存 UnitId，通过“获取单位数据快照”读取；修改快照后调用“更新玩家单位数据”提交。监听处理类 OnUnitDataChanged 刷新 UI。快照不是实时引用，不应长期作为权威数据缓存；提交前重新读取，以免覆盖其他更新。

子系统持有的处理类中，TMap<FGuid, TSharedPtr<FUnitData>> 是唯一权威数据源。C++ 单位实体保存共享引用（或 TWeakPtr），通过子系统 GetUnitDataShared(UnitId) 获取：

```cpp
TSharedPtr<FUnitData> Data = UnitSubsystem->GetUnitDataShared(UnitId);
if (Data.IsValid())
{
    const float Health = Data->RuntimeData.CurrentHealth;
}
```

C++ 优先通过 Data->Modify(lambda) 原地修改并通知，也可直接修改字段后调用 NotifyDataChanged()；不能修改 ID。蓝图快照通过 UpdateUnitData 提交。重新加载同 ID 保持分配地址，已持有的指针立即看到最新值。输入数组及蓝图快照是传输副本，加载后可以丢弃。蓝图使用“根据ID获取单位数据引用”取得 UUnitDataReference 并保存为变量；它内部持有相同智能指针，可绑定 OnDataChanged，通过 GetSnapshot 临时读取。普通结构体变量仍是副本。

所有访问在游戏线程执行。通知期间拒绝重入写入；如需响应通知再修改，请安排到下一帧。共享记录会追踪头像等 UObject 字段，避免 GC 提前回收。强引用可在子系统销毁后延长记录寿命；消费者应在退出游戏时释放，跨 GameInstance 不复用旧引用。ClearPlayerUnits 只清理 Actor 登记，不删除单位数据。尚未实现磁盘存档与网络同步。

验证：`SilverChoir.Player.SharedUnitData` 覆盖批量生成、表查询失败、原地更新、指针身份、加载原子性及 UObject 生命周期。

验证：`SilverChoir.Player.UnitTemplateData` 覆盖模板表、唯一 ID、状态初始化、非法值处理及模板不被修改。



## 共享数据开发约定

完整约定位于 UnitStructs.h 的 FUnitData 注释。FUnitData::OnDataChanged 是原生多播委托，不写入存档，也不随数据拷贝复制。原地加载/更新保留监听器。消费者用 AddUObject 注册，并保存 FDelegateHandle，在换绑、NativeDestruct/EndPlay、BeginDestroy 时解绑。普通 C++ 字段赋值不能自动触发事件，必须统一通过 Modify 或手动 NotifyDataChanged；禁止通知中重入修改。

```cpp
auto Data = UPlayerUnitLibrary::GetUnitDataShared(this, UnitId);
if (Data)
{
    Data->Modify([](FUnitData& Unit)
    {
        Unit.RuntimeData.CurrentHealth = 80.f;
    });
}
```

人员整备室及人员行保存共享数据引用；PersonnelData 仅为兼容旧蓝图的临时展示字段，不是权威 FUnitData。右侧尚未接入单位业务，后续添加时同样遵守共享引用规则。人员行点击成功后触发整备室蓝图可实现事件“人员被点击”(FGuid UnitId)，重复点击已选单位也触发；异地禁用行不触发。自动筛选选中与普通 SelectPersonnel 调用不冒充用户点击。

## 车辆数据

玩家单位管理类现在同时持有 `TMap<FGuid, TSharedPtr<FVehicleData>>` 车辆权威记录，规则与人员一致：同 ID 原地更新，C++ 通过共享指针修改并通知；蓝图快照不自动写回。新增 **从模板表加载玩家车辆** 节点接收模板表和当前瓦片 ID，整表每行生成一辆并追加。小队绑定通过车辆 ID 建立，UI 从此存储查询同瓦片车辆。完整使用与读档顺序见 [PlayerVehicles.md](PlayerVehicles.md)。
