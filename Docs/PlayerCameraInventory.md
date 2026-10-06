# 玩家相机与背包接入

`APlayerCameraPawn` 位于 `Public/SubSystem/PlayerSubSystem`，继承 FreeCameraSystem 的 `AFCS_FreeCameraPawn`。
它通过原生默认子对象持有 `UnitInventoryComponent`（`USIS_UnitInventoryComponent`）。
`AGameMainMapPlayerController` 同样持有 `PlayerInventoryManager`（`USIS_PlayerInventoryManager`）。

现有 `/Game/System/SubSystem/PlayerSubSystem/BP_PlayerPawn` 已改为继承 `APlayerCameraPawn`，保留原有相机配置。
主地图继续使用玩家配置中的 PlayerPawnClass 生成和控制该 Pawn；配置留空时使用原生 APlayerCameraPawn。
背包网格和初始物品可在 BP_PlayerPawn 的继承组件 UnitInventoryComponent 中配置，本次未添加默认物品。

蓝图使用“获取玩家相机”（PlayerLibrary.GetPlayerCamera），返回当前受控 APlayerCameraPawn；再读取 UnitInventoryComponent。
未生成相机、主菜单、无有效世界上下文时可能返回空，应先 Is Valid。
玩家子系统按当前 World 查询受控 Pawn，不缓存跨地图失效对象，也不会额外生成第二个相机。

验证：SilverChoir.Player.CameraInventory 检查主地图实际生成的 Pawn 类型、函数库查询、两个组件的注册与所有者，以及重复组件。

## 相机瞬移与子地图偏移（2026-10-02）

蓝图函数库提供三个配合使用的节点：

| 节点 | 输入与用途 |
| --- | --- |
| 地图切换 → 根据MapID获取地图加载位置 | 输入 `Base`、`T5` 等实例 ID，输出实际登记的加载位置和“找到地图”。找不到时返回 false、零向量。 |
| 玩家 → 相机 → 为相机状态添加位置偏移 | 输入 `CameraState` 与 `LocationOffset`，返回位置相加后的副本；角度、臂长和各项速度均保留。 |
| 自由相机 → 立即设置自由相机状态 | 一次应用位置、Yaw、Pitch 和臂长，取消正在执行的状态移动及旧缩放缓动，当帧刷新弹簧臂。暂停时也可调用。 |

瞬移节点的“切换后恢复相机臂缓动”默认不勾选：位置和旋转缓动保持关闭。勾选后，瞬移仍立即完成，后续移动恢复调用前的缓动设置。旧移动的完成回调不会再执行；返回 true 就代表这次瞬移已应用。仍遵守当前相机的边界、俯角、臂长限制及碰撞配置，不改变手动输入锁。

场景相机的调用顺序是：查询地图加载位置 → 为原始相机状态添加位置偏移 → 移动自由相机到状态。需要直接切镜头时，最后一个节点换成“立即设置自由相机状态”。例如地图加载在 `(0,0,-20000)`，原始相机位置 `(100,200,100)` 将变为 `(100,200,-19900)`。

现有基地全景、小队会议室、人员整备室和作战指挥室共 10 处相机移动已接入该流程。每个场景蓝图的“相机所属地图ID”变量 `CameraMapID` 默认是 `Base`，原有相机参数保留为地图内坐标。进入上方后再缩放、退出时使用独立速度等原有流程不变。团长办公室尚无相机移动节点。

偏移只在执行时相加，不写回配置变量；“获取当前相机状态”已经是世界坐标，不能再次叠加加载位置。此函数只处理平移，不转换地图旋转。

新游戏 Handler 的基地入场变换同样先加实际加载位置，再传给 `ActivateLoadedMap`。控制器的原生 `EnterMap` 会同步刷新弹簧臂，避免跨地图传送后的相机追赶，并保留后续正常操作的缓动配置。需要完全关闭后续缓动时显式使用上述瞬移节点。

当前新游戏的基地加载位置在 `BP_MapTransitionHandler_NewGame` 的 `SubMapConfigs` 数组中设置；修改 `Base` 项的 `Location` 即可。本次未将保存的基地高度固定为 -20000。

### 验证记录

`SilverChoirEditor Win64 Development` 编译成功；4 个场景及新游戏 Handler 保存后，另开命令行进程重新加载并编译检查通过（`CAMERA_OFFSETS_VALIDATE_OK`）。现有 EasyHouseBuilder 的 ToolsetRegistry 元数据错误仍会让命令行进程退出码为 1；本次相关蓝图没有编译错误或警告。

11 项自动化测试通过：`FreeCameraSystem.Camera.*` 4 项、`MapTransitionSystem.*` 5 项，以及两个实际蓝图流程：

- `SilverChoir.Camera.MapOffsets.RuntimeFlow`：临时将真实新游戏加载请求设置为 `(400,-900,-20000)`，检查 4 类场景、共 9 次到达/返回及重复进入，无偏移累加；同时检查偏移函数保留角度、臂长、速度且不修改源状态。测试仅改内存配置，结束后恢复。
- `SilverChoir.BaseUI.OperationsCommandRoom.SubMapEntry.RuntimeFlow`：暂停时进入 T5、替换旧区域、基地持续驻留、重复进入及取消等待回归。移除 MapConfig 后该测试同时检查未指定地图类型的错误，以及插件卸载不再自动返回基地。

日志在 `Saved/OperationsPanelUpgrade/CameraStateToolsCore.log`、`CameraMapOffsetsVerified.log`、`CameraSnapSubMapRegression.log`。修改前的原生文件和蓝图资产备份在 `Saved/CameraStateTools/Backup`。未执行完整打包。
