# GameMainMap 主地图架构

GameMainMap 是常驻 UWorld。基地和战斗地图通过 MapTransitionSystem 的
`UMTS_SubMapSubsystem` 加载为动态关卡实例，切换不会重建 GameMode、控制器或 PlayerState；基地/战斗 UI 随显式激活切换。
这适合独立基地与战斗区域；不是多个 UWorld 同时运行，也不是 World Partition 实现。

## 资产与类

C++ 位于 `Source/SilverChoir/Public/Map/GameMainMap` 和对应 Private 目录。
蓝图、宿主地图位于 `/Game/System/Map/GameMainMap`。

| 类型 | 职责 |
| --- | --- |
| GameMainMapGameMode | 激活已就绪的地图、保护激活事件重入；不发起加载、卸载或自动返回 |
| GameMainMapPlayerController | 本地地图 UI、EnterMap 角色与相机扩展 |
| GameMainMapGameState | ActiveMapID、CurrentMapType、同步激活保护 bSwitchingMap、LastError 及完成/失败事件 |
| GameMainMapPlayerState | 单次游戏会话玩家状态扩展基类；跨 OpenLevel 数据继续使用玩家子系统/存档 |
| MapWidgetBase | UIBasic 中的通用地图 UI 基类；基地/战斗子类继承可选 HUDLayer / ModalLayer 与 InitializeMapUI |
| GameMainMapSubMapHandler | 提交初始化、激活本地图、等待进入前场景；现位于 SubSystem/GameMapTransitionSystem |
| GameMapSubMapHandler | T5 等游戏地图的默认进入流程、战斗 UI 初始化、有序小队生成器注册与部署；配置由子蓝图提供 |

`Scripts/create_game_main_map.py` 只创建缺失资产，不重置已存在的蓝图或地图。
当前基地与战斗模板分别位于 `/Game/System/Map/BaseMap/L_BaseTemplate` 和 `/Game/System/Map/BattleMap/L_BattleTemplate`。
默认 Pawn 已接入 FreeCameraSystem。GameMode 从玩家子系统读取 PlayerSettings.PlayerPawnClass，当前为 `/Game/System/SubSystem/PlayerSubSystem/BP_PlayerPawn`。
在“项目设置 → SilverChoir → 玩家配置 → 玩家 Pawn 类”更换类型；GameMode 的 DefaultPawnClass 仅作对应显示/回退，运行时优先使用玩家配置。
相机参数资产是同目录 `DA_PlayerCameraConfig`，可调整速度、俯角、臂长等；保留插件默认键鼠绑定。
蓝图通过玩家函数库“获取玩家 Pawn”获取当前控制的 Pawn，或通过插件函数库“获取自由相机”调用相机接口。
玩家子系统按当前 World 查询 Pawn，不强引用旧世界；Pawn 由 GameMode 生成到常驻关卡，基地/战斗流式切换保持同一实例。
UI 的 InitializeMapUI 提供 GameState，绑定事件前也读取一次当前状态。

## 配置与调用

1. 新游戏在 `BP_MapTransitionHandler_NewGame.SubMapConfigs` 配置基地等初始子地图，通过插件 `LoadSubMaps` 加载。
2. 运行中的瓦片地图在自己的 Handler 蓝图配置 `LoadRequest`。T5 使用 `BP_SubMapHandler_T5`，包含插件子地图资产、位置旋转、加载界面等；插件中 MapID 唯一。
3. `Location/Rotation` 是地图实例变换；`LocalEntryTransform` 是地图内进入点，Handler 根据实际已加载实例变换计算世界进入点。
4. 主地图 World Settings 使用 `BP_GameMainMapGameMode`。直接 PIE 启动宿主不自动加载基地，初始化由蓝图/插件安排。
5. 地图初始化完成且已显示后调用 `ActivateLoadedMap(MapID, WorldEntryTransform, MapType)`，必须显式传入 `Base` 或 `Battle`。返回 true 表示同步激活完成；`OnActiveMapChanged` 通知成功。
6. 控制器蓝图可覆盖 `EnterMap` 实现队伍生成和相机逻辑；原生实现传送已有 Pawn 并刷新相机臂。角色归属常驻关卡，不能归属即将卸载的地图。
7. GameMode 的 `OnMapActivated(Previous, Current)` 可管理区域 AI、音效与模拟。子地图自己的 GameMode 不会启用。

T5 默认生命周期现已迁入 `UGameMapSubMapHandler`，小队部署接口与蓝图接法见 `Docs/GameMapTransitionSystem.md`。

异步加载、卸载、并发保护和加载界面生命周期由 MapTransitionSystem 管理。T5 入口查询瓦片区域标签并通过插件卸载选中的旧区域，基地不在该集合中。加载忙碌状态查询插件 `IsSubMapTransitionInProgress`；GameState.bSwitchingMap 仅用于同步激活期间防止事件重入。

未指定或非法的 `MapType` 会记录 Error 日志、写入 GameState.LastError、广播 OnMapSwitchFailed 并返回 false，不修改地图 ID、地图类型、玩家位置或 UI。成功激活会清空 LastError。

## 快速切换与内存

保留地图时复用插件注册表中的实例与 Handler；需要释放时调用 Handler.UnloadMap，或插件按 ID/Tag 卸载节点。GameMode 不再管理自动卸载策略。
当前 T5 卸载不会自动返回基地，也不隐式更换活动 ID、地图类型、玩家位置或 UI；战斗返回基地的业务流程后续实现。调用方需要自行安排目标地图激活与卸载时机。
驻留数量没有自动上限，需按目标平台预算释放。卸载后的战斗状态需由自己的数据/存档恢复。

目前驻留地图保持加载且显示，活动地图 ID 只是游戏逻辑状态，不会自动隐藏、暂停或隔离关卡。
地图应空间隔离；非活动地图的 AI、Tick、计时器、音效、天气和全局灯光必须自行管理。
不要用 SetGamePaused 暂停单个子地图，它会影响整个世界。仅隐藏可见性也不是保存/暂停战况方案。
基地与战斗不宜各带一套同时生效的全局天空/后处理；优先宿主统一管理或激活时明确切换。

## 与整图切换结合

当前新游戏已指定 `/Game/System/Map/MainMenu/MapTransitionHandler/BP_MapTransitionHandler_NewGame`。
其“主地图加载完成”事件调用插件 LoadSubMaps，参数为类默认值 SubMapConfigs 结构体数组。
每项配置 ID、权重、地图、位置、旋转；默认 Base 指向 L_BaseTemplate。数组节点自动登记统一进度并并行加载。
全部子地图加载完成后，蓝图调用 ActivateLoadedMap(Base, EntryTransform, Base) 更新 GameState 和安置玩家，再主动通知完成；插件等待 1 秒后关闭加载页。
EntryTransform 是玩家的世界进入点，不是地图实例位置。新游戏 Handler 已将地图内入场变换的位置加上按 MapID 查询的实际加载位置，再传入该参数；该 C++ 节点不重复加偏移，也不加载或卸载地图。
主地图占 30%，数组子地图按权重分配剩余 70%。详见 `Plugins/MapTransitionSystem/Docs/统一子地图加载进度.md`。
GameMode 不订阅子地图加载状态，也不再维护一套异步加载状态机。
运行中可通过 MTS 的 Key 查询/隐藏/显示接口复用已加载地图，再由蓝图调用 ActivateLoadedMap。
可见性请求异步生效，须等待实际可见后激活。隐藏不等于保存或暂停游戏逻辑。

地图切换函数库新增“根据MapID获取地图加载位置”（`GetMapLoadingLocation`）。它查询当前世界子地图注册表的实际实例变换，适用于 `LoadSubMaps`、单地图加载和 Handler 加载；不是读取可能已变化的配置资产。加载中和隐藏的已登记地图也能返回位置，因此返回 true 只表示找到了 ID，不表示加载或可见性已就绪。未知 ID、无世界上下文或卸载注销后返回 false 和零向量。

房间相机对加载偏移的接入及瞬移节点用法见 `Docs/PlayerCameraInventory.md`。

当前面向单机，未实现联网请求、客户端实例同步，也未验证 World Partition/嵌套关卡。
打包地图列表包含宿主及两个模板；新增真实地图后同步 Packaging 或 Asset Manager Cook 配置。

## 验证

`SilverChoir.MenuTravel.StartAction` 验证实际主菜单切图、蓝图基地加载、30%/70% 统一进度、主动通知与 1 秒关闭延迟。
`MapTransitionSystem.SubMaps.BatchConfigs` 验证整批校验、多个地图实例、位置旋转和完成通知。
`MapTransitionSystem.SubMaps.RegistryVisibility` 验证按 Key 隐藏、恢复同一实例和卸载后注销。
测试使用空白地图，不代表实际大地图性能已验证。

## 地图状态与本地 UI

主地图不再创建常驻 UI，MainWidgetClass、MainWidget 和 WBP_GameMainMap 已移除。
原生 GameMainMapWidget 改为通用 UIBasic/MapWidgetBase，保留类重定向以兼容已有基地/战斗 UI 子蓝图。
当前地图类型 EGameMainMapType 与 CurrentMapType 存放在 GameMainMapGameState。
蓝图通过主地图函数库的“获取主地图对象”取得 GameMode、GameState、玩家控制器、玩家状态和 Pawn，也可使用各对象的独立节点；“获取当前地图类型”直接返回 EGameMainMapType。修改类型仍调用 GameState 的 SetCurrentMapType。
原来接在 PlayerController 上的状态节点需要改接 GameState。
PlayerController 只持有本地 BaseWidget、BattleWidget 和它们的类配置；SwitchMapUI 负责互斥显示，不改变实际地图状态。
ActivateLoadedMap 成功后统一设置 GameState、安置玩家并调用本地控制器 SwitchMapUI；关闭 UI 不代表离开当前地图。
ActivateLoadedMap 的 MapType 必须明确为 Base 或 Battle；None 直接报错，不推断地图类型。
当前项目为单机；如以后多个玩家分别位于不同地图，每名玩家的位置状态应移入 PlayerState，而不是共享 GameState。


控制器原生 BeginPlay 激活通用输入映射，并订阅 GameState 的地图类型变化；Base/Battle 使用各自的模式映射，None 只保留通用映射。设置见 Docs/MainMapInput.md；沙盘输入与作战指挥室场景流程均由蓝图自己的变量、函数实现。

## 旧配置移除（2026-10-02）

已移除 GameMode.MapConfig、UGameMainMapConfig、FGameMainMapEntry、DA_GameMainMapConfig，以及 GameMode/PlayerController 的 EnterBattle、UnloadInactiveBattle 和旧加载状态机。Handler.ReturnPlayerToBase 与 T5 卸载事件中的自动返回调用一并移除；旧 PendingMapID 和指向已删除 API/配置的重定向也已清理。资产生成脚本不再重建这些配置或节点。

原文件与资产备份位于 `Saved/RemoveLegacyMapConfig/Backup`。`DA_BaseMap`、`DA_BattleExample` 等插件子地图定义继续保留，GridStrategyMapSystem 的沙盘 MapConfig 不属于本次移除范围。

验证：Editor Development 编译成功；扫描 171 个项目蓝图，旧接口节点及配置资产引用均为 0，地图目录下的蓝图重新编译通过。独立游戏进程的 `OperationsCommandRoom.SubMapEntry.RuntimeFlow` 与 `Camera.MapOffsets.RuntimeFlow` 均通过，覆盖省略/非法地图类型拒绝且不改变现场、显式激活、暂停进入 T5、重访、无自动返回的卸载，以及 Z=-20000 下的各房间相机移动。

运行日志：`Saved/OperationsPanelUpgrade/RemoveMapConfigSubMapFlow.log`、`RemoveMapConfigNegativeHeight.log`。蓝图验证日志为 `LegacyMapConfigValidateBeforeDelete.log`；其唯一 Error 来自已有 EasyHouseBuilder ToolsetRegistry 元数据注册，与本次蓝图清理无关。未执行完整打包。

## 战斗地图 UI（2026-10-02）

`BP_GameMainMapPlayerController.BattleWidgetClass` 已配置为 `/Game/System/Map/BattleMap/UI/WBP_BattleHUD`。通过插件加载子地图，再调用 `ActivateLoadedMap(..., Battle)` 后会自动显示；无需在关卡中再创建 Widget。旧 `BP_BattleMapWidget` 资产保留，但不再作为控制器的默认战斗 UI。

设计器中的底部面板高 148，1920×1080 下最大宽 1800。包含左侧小队选择、六张成员卡、成员指令面板、车辆竖条和模式按钮。点击成员卡后，指令面板从该成员右侧展开；车辆面板从成员行末端展开，两者互斥。无载具小队禁用车辆按钮。右上角 Pad 当前显示战术示意图，可跟随、切换网格、缩放和折叠，尚未连接真实地图捕获。

可编辑资产：

- `UI/WBP_BattleHUD`：整体 Designer 布局、初始化和业务事件图 `BattleUIBusiness`。
- `UI/Components/WBP_BattleMemberCard`：正方形头像、细竖向体力/士气条、心电图及双手装备区。
- `UI/Components/WBP_BattleIconButton`：通用图标按钮，支持窄竖条文字。
- `UI/Data/DA_BattleHUDTestUnits`：三支测试小队（6、4、3 人），涵盖受伤、双手武器、独立左右手、有车和无车状态。

`WBP_BattleHUD` 的蓝图变量 `UseTestUnits` 默认为 true，`TestUnitData` 指向上述测试数据。测试数据只复制到 UI 的展示状态，不创建正式人员、不修改玩家小队存档，也不会消耗真实库存。

四个快捷物品可点击使用；人员指令展开且 HUD 拥有键盘焦点时，也可按 1–4 使用。Esc 优先取消目标选择，其次关闭测试弹窗，最后收起指令面板。

C++ 分工：

- `BattleHUDLibrary` 计算成员/抽屉布局，校验比例，并通过相同有效 ItemId 判断左右手是否合并；两只空手不会合并。`ReadHandEquipment` 可从库存插件的左右手装备槽读取 ItemId，接入时传入实际配置的两个槽位 Tag。
- `BattleMapWidget` 管理选择、面板互斥与动画、展示状态、光标及目标命中；`BattleHUDWidgets` 绘制小图标、心电图和示意地图，头像复用统一的正方形裁切函数。心电图只刷新绘制，不逐帧重排布局。
- 测试背包、快捷物品和上下车节点仅演示 UI 快照变化。姿态和潜行目前更新展示数据，尚未驱动真实 Pawn、AI、库存或车辆。

接入真实业务时：

1. 在 `WBP_BattleHUD.InitializeMapUI` 中关闭测试分支，取得实际小队数据，构造 `FBattleSquadView` / `FBattleMemberView` 后调用 `SetBattleSquads`。SquadId、UnitId 必须有效且唯一；刷新单个人员使用 `UpdateMemberView`，无需每帧重建整表。
2. 在蓝图“战斗操作请求”（`OnBattleCommandRequested`）事件中按 `Command` 分支，将 UnitId / SquadId 接到实际姿态、背包、潜行、物品和车辆功能。初始化策略和这些业务连接均在蓝图中。
3. 交互或拾取分支先调用 `BeginTargeting`。左键命中场景后触发蓝图“战斗交互目标确认”（`OnBattleTargetConfirmed`），携带 UnitId、Command 和 HitResult；在此判断对象并执行交互/拖拽。右键或 Esc 取消，关闭 UI 时恢复光标。
4. 小队控制模式按钮目前保留蓝图入口及提示，当前版本实现成员控制模式。装备区展示合并状态，实际拖放换装由后续库存业务接入。

验证覆盖：`SilverChoir.BattleUI.LayoutAndHandOccupancy` 和 `SilverChoir.BattleUI.RuntimeFlow`。后者走插件子地图加载与真实 `ActivateLoadedMap` 入口，检查蓝图初始化、六人成员布局、人员/车辆面板切换、测试指令、无车禁用、数据校验、光标清理与测试数据不持久化。运行截图保存在 `Saved/Screenshots/BattleHUD`。

Editor Development 编译、上述两项测试、两项通用按钮回归测试，以及既有 `OperationsCommandRoom.SubMapEntry.RuntimeFlow` 均已通过。后者覆盖暂停状态下从作战指挥室进入 T5、保留基地、重访和卸载。日志为 `Saved/OperationsPanelUpgrade/BattleHUDFinal.log`、`BattleHUDButtonRegression.log`、`BattleHUDT5Entry.log`。同时修复通用按钮自绘边框的缓存失效问题，使选中/悬停颜色及时刷新；未执行完整打包。
