# 小队会议室 UI

小队会议室使用 C++ 基类管理层级、共享数据、列表与动画；子蓝图 Designer 保存可编辑布局。顶部货币区、标题与底部返回基地栏继续由 `BP_BaseMapWidget` 提供。

## 资产与代码

主界面：

`/Game/System/Map/BaseMap/UI/SceneUI/SquadMeetingRoom/WBP_SquadMeetingRoom`

组件位于该目录的 `Components`：

| 蓝图 | 用途 |
| --- | --- |
| `WBP_SquadListEntry` | 小队列表行：队徽、名称、成员数量、瓦片位置 |
| `WBP_SquadMemberCard` | 下方横向席位卡：实员显示头像、代号和操作；空位显示编号与待编入状态 |
| `WBP_SquadIconOption` | 预设队徽选择按钮 |
| `WBP_SquadVehicleEntry` | 车辆选择卡：16:9 图片、名称、座位、位置和可选状态 |

人员列表复用 `/Game/System/Map/BaseMap/UI/SceneUI/PersonnelPreparationRoom/Components/WBP_PersonnelListEntry`，头像复用同目录的 `WBP_PersonnelPortrait`。普通操作按钮使用 `/Game/System/UIBasic/WBP_ShellButton`，待命/全部页签使用 `/Game/System/UIBasic/WBP_SelectionButton`，沿用原有悬浮音与按下音反馈。

C++ 位于：

```text
Source/SilverChoir/Public/Map/BaseMap/SceneUI/SquadMeetingRoom/
    SquadMeetingRoomWidget.h
    Components/SquadMemberCardWidget.h
    Components/SquadVehicleEntryWidget.h
```

实现位于对应 `Private` 目录。小队数据及规则见 `Docs/PlayerSquadSubsystem.md`。

## 接入基地场景

`BP_BaseMapWidget.SceneUIClasses` 使用 `GameScene.SquadMeetingRoom` 映射到主界面。

场景业务由 `/Game/System/Map/BaseMap/Scene/BP_小队会议室_Scene` 的可编辑蓝图图表负责。正常通过场景管理器切换到 `GameScene.SquadMeetingRoom`，即可执行相机移动、创建界面、初始化列表和完成通知。若从其他功能单独创建界面，接线顺序如下：

1. 取得 `BP_BaseMapWidget` 实例。
2. 调用 **根据场景Tag创建子UI**，传入 `GameScene.SquadMeetingRoom`。
3. 将返回对象转换为 `SquadMeetingRoomWidget` 或 `WBP_SquadMeetingRoom`，保存此实例。
4. 调用 **展示小队列表**，将实际的当前战略瓦片 `FName` 传入 `TileId`。
5. 检查节点的布尔返回值；失败原因可从 `LastError` 或 **小队操作失败** 事件获取。

玩家单位和车辆数据应先加载到玩家单位子系统。恢复已有小队存档时，先恢复单位、车辆，再恢复小队；小队中的车辆 ID 必须能查询到已加载的玩家车辆。

`TileId` 必须明确传入：界面不会从关卡名称、Actor 位置、列表第一个单位或任意已有小队推导当前位置。传入 `None` 会显示提示并禁用新建。调用 **展示小队列表** 会回到选择小队层，丢弃尚未保存的草稿、清除旧选择并恢复“待命”筛选；可以在创建子 UI 后的入场动画期间调用。

场景进入和离开均返回 `false`，等待相机、UI 各自完成。不要在 `OnUnloadCompleted` 中直接同步调用 **通知离开场景完成**：此时 BaseWidget 仍在广播回调并清理旧 UI，场景管理器若同步进入下一个场景，会导致下一界面的创建被切换锁拒绝。小队场景在两项完成后通过事件图的 `Delay Until Next Tick` 延后通知，再由场景管理器进入下一场景。

### 场景进入与退出

进入沿用人员整备室的两段相机路径：先移动到区域上方，再同时移动到房间内、创建小队 UI。创建成功后绑定本次界面的 `OnLoadCompleted` 和 `OnUnloadCompleted`，调用 **展示小队列表** 并隐藏区域块。界面入场完成、房间内相机移动完成均满足后，显示返回基地按钮并通知进入完成。

退出时隐藏返回基地按钮，同时开始相机返回区域上方和当前层级面板的退场动画。只卸载本场景持有且仍挂载在 BaseWidget 中的界面，避免误关闭别的场景。两项均完成后解绑自身的回调、清空引用，延至下一 Tick 通知离开完成。下一场景负责自己的相机、区域块和返回按钮状态。

相机节点返回 `false` 不会回调，蓝图已为此补上完成分支；控制器或 UI 创建失败也会记录 `LastFlowError` 并结束相应等待，允许继续切换离开，不会永久卡在场景管理器的进入/退出状态。流程阶段会过滤重复或过期回调。

宿主复用已经加载的同 Tag 界面时，`OnLoadCompleted` 不会重播。新只读节点 **场景UI是否已加载完成** `IsSceneUILoaded` 用于绑定之后检查这种情况，已完成则直接满足 UI 门槛；仍在入场时继续等原事件。

蓝图类默认值中配置：

| 变量 | 用途 |
| --- | --- |
| `CurrentTileId` | 当前战略瓦片，初始为人员整备室现有配置的 `T5`；接入战略地图后改为实际位置来源 |
| `OverheadCameraState` | 区域上方相机状态，保留原小队场景局部变量的值 |
| `InteriorCameraState` | 房间内部相机状态，保留原小队场景局部变量的值 |

`RoomUI`、`BaseUI` 为当前实例引用；`UiFinish`、`CameraFinish`、`FlowStage` 为流程状态，不需要手动赋值。列表初始化失败会在界面显示现有业务提示，仍正常等待入场动画结束。场景切换需要等待当前流程完成；运行中的相机移动不应被其他系统任意替换或取消，因为自由相机插件取消移动时不会发送原完成回调。

## 两个界面层级

| 状态 | 左侧 | 右侧 | 中间下方 |
| --- | --- | --- | --- |
| `SquadSelection` 选择小队 | 小队列表、待命/全部、新建小队 | 收起 | 收起 |
| `SquadManagement` 管理小队 | 人员列表、待命/全部、返回小队列表 | 可点击队徽、队名输入、位置、车辆入口、底部固定保存按钮 | 当前草稿成员与剩余空位，数量合计等于容量 |

点击小队后先滑出当前内容，再将左侧切换为人员列表，并滑入右侧管理面板与下方队员名单。返回小队列表按相反层级执行。`CurrentLayer`、`SelectedSquadId`、`CurrentTileId`、`bTransitioning`、`bEditingDraft` 和 `bNewSquadDraft` 可供蓝图读取。

“待命”仅显示当前瓦片的小队或人员；“全部”包含异地对象，异地条目显示禁用状态，不能选中或加入。管理层只允许编辑当前瓦片的小队。其他小队中的人员以暗金色显示原小队名称；点击同地点的其他小队成员，先弹出含人员和原队名的调离确认框，确认后加入当前界面草稿，原小队归属保持不变。点击 **保存小队** 后，小队管理类统一校验并转队。校验失败保留草稿和全部原始数据。

左右面板使用比例锚点，中间保留展示空间。右侧管理内容支持竖向滚动，下方名单支持横向滚动；不通过压缩人员卡高度容纳更多成员。右侧顶部使用 128×128 的可点击队徽框，内部图片为 112×112；右边集中放置小队名称输入框和当前位置。旧的独立队名编辑区与“保存名称”按钮已折叠。**保存小队** 和草稿状态提示固定在右侧底部，窗口高度变小时正文滚动，保存入口不随正文滚走。

## 编辑草稿与保存

打开已有小队时，界面记录原始小队快照并建立编辑草稿；新建时只建立草稿。队名、队徽、车辆、成员、队长的界面操作只修改草稿，不立即修改玩家小队子系统或其他单位的所属小队。

- **保存小队** `SaveSelectedSquad()` 调用管理类的 `CommitSquadDraft` 一次提交全部修改。成功后先启动现有的返回流程，随后立即触发 **小队已保存** `OnSquadSaved(SquadId)`，不等待动画结束。返回流程会让管理面板和成员名单滑出，再让左侧恢复小队列表。保存失败时不返回，保留编辑和错误提示。需要等待列表完全显示的业务使用 `OnLayerChanged(SquadSelection)`。
- 新建节点返回 `true` 表示成功打开新队草稿，`OutSquadId` 和 `SelectedSquadId` 仍为无效 ID。保存成功时才分配正式 ID；需要新队 ID 的业务应接到 `OnSquadSaved`，或保存成功后读取 `SelectedSquadId`。
- 返回小队列表、关闭场景 UI、重新展示列表或选择其他小队都会丢弃未保存修改，不自动保存。返回名册时在管理面板滑出后清除草稿，关闭整个界面时保留已显示的控件内容完成退场动画。
- 按 Enter 只确认名称输入，不提交整个小队。底部状态提示说明是否存在未保存修改；`HasUnsavedSquadChanges()` 可供蓝图判断。
- 保存失败时显示 `LastError`，草稿继续保留以便修改。已有小队在编辑期间被其他业务修改，会因原始快照过期而拒绝保存，避免覆盖外部变更；重新选择小队可取得最新数据。

这些草稿是 `FSquadData` 快照，其中成员只保存 ID，不产生 `FUnitData` 副本；`AssignedVehicle` 是车辆显示与草稿兼容快照，正式关联以其中的 `VehicleId` 为准，提交时重新查询玩家单位子系统中的权威车辆。尚未保存的新队不会出现在子系统查询或存档快照中。脏状态通过 `MatchesSnapshot` 比较完整快照。

管理类提交时会同步通知单位和小队监听器。若监听器在提交过程中选择了另一个小队或重新展示列表，原保存调用仍返回成功，但不会覆盖新草稿或启动原来的返回流程；界面仍活跃时，`OnSquadSaved` 只报告本次真正保存的 ID。若监听器已开始关闭界面，则不再触发该界面的保存事件。提交使用独立输入快照，后续 UI 处理通过草稿代次检查上下文。

更换或丢弃草稿时，会取消人员行、小队行、成员卡、图标选项及操作按钮尚未触发的延迟点击，防止在小队 A 按下的操作落到随后选中的小队 B。取消操作不释放退场中的单位头像与引用；普通数据刷新不会执行整批取消。

## 可调用节点

以下节点的目标为当前 `SquadMeetingRoomWidget` 实例：

| 蓝图节点 | 输入与行为 |
| --- | --- |
| 展示小队列表 `LoadSquadList` | `TileId`：设置当前瓦片并显示小队选择层 |
| 在当前瓦片新建小队 `CreateSquadAtTile` | `TileId`、`SquadName`；打开新队草稿，`OutSquadId` 在保存前无效 |
| 选择小队并打开管理面板 `SelectSquad` | `SquadId`；取得当前瓦片小队的原始快照和编辑草稿 |
| 返回小队列表 `ReturnToSquadList` | 收起管理层，丢弃未保存草稿并清除当前选择 |
| 设置小队会议室全部筛选 `SetShowAll` | `true` 为全部，`false` 为待命 |
| 将人员加入当前小队 `AddUnitToSelectedSquad` | 显式算法入口：`UnitId` 直接加入草稿，不自动询问；人员行的用户点击会先走转队确认，保存成功后才正式转队 |
| 确认人员转入当前小队 `ConfirmPendingUnitTransfer` | 校验待确认人员/原队/当前位置/容量后加入草稿；没有待确认请求时返回 false |
| 取消人员转队确认 `CancelPendingUnitTransfer` | 关闭确认框，不改变成员草稿与正式归属 |
| 是否正在确认人员转队 `IsUnitTransferPending` | 查询模态窗口状态；可用 `GetPendingUnitTransferId` 获取对应人员 ID |
| 从当前小队移出人员 `RemoveUnitFromSelectedSquad` | `UnitId`；仅从当前草稿移出，保存前原归属不变 |
| 将当前小队成员提升为队长 `PromoteMemberToCaptain` | `UnitId`；必须属于草稿成员，只更改草稿队长 |
| 修改当前小队名称 `RenameSelectedSquad` | `Name`；更新草稿名称，正式保存时校验名称非空 |
| 选择当前小队预设图标 `SelectPresetIcon` | 从 `0` 开始的 `IconIndex`；修改草稿并关闭选择弹窗 |
| 使用当前队长头像作为图标 `UseCaptainPortrait` | 草稿启用动态队长头像来源并关闭选择弹窗 |
| 设置当前小队车辆 `SetSelectedSquadVehicle` | `Vehicle`；按其中的 ID 查询权威车辆，验证位置、座位和正式小队占用，仅修改草稿并返回缩容移出的 `OutRemovedUnitIds` |
| 加载当前瓦片车辆列表 `LoadAvailableVehicles` | `TileId`；从玩家单位子系统读取此瓦片的车辆，异地车辆不显示 |
| 清除当前小队车辆 `ClearSelectedSquadVehicle` | 恢复默认四人容量，仅修改草稿；返回超出容量而移出的 `OutRemovedUnitIds` |
| 获取当前小队人数上限 `GetSelectedSquadCapacity` | 无车辆为 4，有车辆为含驾驶席的载员上限，未编辑草稿时为 0 |
| 保存小队 `SaveSelectedSquad` | 原子提交草稿；成功后开始返回小队列表并触发 `OnSquadSaved`，失败保留编辑 |
| 小队是否有未保存修改 `HasUnsavedSquadChanges` | 判断当前草稿；新队在首次保存前始终为 `true` |
| 显示小队队徽选择框 `SetIconPickerVisible` | `bVisible`；管理层允许操作时打开，`false` 关闭 |
| `GetSelectedSquad` | 返回当前编辑草稿快照；未编辑时返回 `false`，不返回单位数据副本 |

UI 中的新建按钮使用当前 `CurrentTileId`，从玩家小队配置中的名称表依次选取未使用名称，随后可在管理面板改名。配置名称用完后，按 `第X小队` 接续编号；例如 24 个有效且不重复的名称后从 `第25小队` 开始，并跳过已占用的编号。`CreateSquadAtTile` 的名称留空时也使用推荐名称；显式名称保持原样。名称只在正式保存后被视为占用，取消草稿不消耗条目或编号。主动调用新建节点时，传入的 `TileId` 必须与先前展示列表的当前位置相同；需要新位置时先调用 **展示小队列表**。动画过程中暂停修改操作，布尔节点此时返回 `false`，应在层级切换结束后再调用。

## 成员与队长

选择未编队人员直接加入草稿；属于其他小队的人员先确认调离。下方横向名单随草稿更新，正式归属在保存后变化。成员卡宽 122、高 204 UMG 单位，间隔 8：顶部单行姓名，中间 52×52 头像及队员/队长身份，下方纵向排列 **升为队长** 与 **移出小队** 按钮。长姓名省略并可悬浮查看完整名称。当前草稿队长使用暗金色 `#B69B61` 的身份文字和顶部细线，其提升按钮显示“当前队长”并禁用。普通队员保持蓝灰色。颜色可在成员卡蓝图类默认值的「小队 → 样式」调整。空间不足时仍可横向滚动，不通过压缩卡片容纳超出宽度的席位。

转队确认期间禁用会议室背景操作、取消已有延迟点击，并将键盘焦点放到取消按钮；Esc 取消，Tab/方向键在两个按钮间切换。确认后左侧保持暗金色“原小队名 · 待调入”，直到保存成功。原队改名时提示文本更新；人员所属小队、位置或当前草稿变化时，旧确认失效。已经确认后又被其他业务转队的人员会在保存时被拦截，需要先从草稿移出并重新选择确认。返回列表、关闭或更换草稿会清除待确认请求，不操作正式归属。

确认框直接在 `WBP_SquadMeetingRoom` 中编辑：`TransferConfirmPanel` 为根层面板，`TransferConfirmText` 为正文，`ConfirmUnitTransferButton` / `CancelUnitTransferButton` 复用通用基础按钮。保持这些名称和兼容类型；正文支持滚动，窗口较小时整体缩放。`SquadTransferUIAssets -Build` 增量补齐确认框并创建缺失的名称表，保留原有图表、动画和所有原控件位置，保存前备份至 `Saved/SquadTransferUIBackups`；已有名称表不会被覆盖。`-Inspect` 只读检查。

小队无车辆时最多 4 人；选定车辆后容量等于 `AssignedVehicle.Attributes.PassengerCapacity`，包含驾驶人员。零座车辆合法，此时人数上限为 0，不显示空位卡。底部标题显示“当前人数 / 容量”，小队列表也显示此信息。满员时左侧未加入草稿的人员显示禁用和“当前小队已满”；已在草稿中的成员继续保留交互。直接调用加入节点同样检查容量，不能绕过限制。

人员行使用 `SetInteractionAllowed` 保存宿主的额外交互许可，始终与本地瓦片资格共同判断。单位修改通知刷新头像、名字和状态时不会覆盖满员限制；默认许可为 `true`，人员整备室原有异地禁选规则保持不变。复用人员行切换当前位置时通过 `SetCurrentTileId` 更新资格，不重绑单位智能指针。

底部按加入顺序显示成员，后面补足空位卡直到容量。例如无车小队已有 2 人时，显示 2 张实员卡和“空位 03”“空位 04”。空位使用原 `WBP_SquadMemberCard` 布局，头像淡化、身份显示“待编入”，操作按钮隐藏但保留位置。空位不持有单位智能指针、不绑定单位修改事件，也不会触发移出或提升事件。

成员卡的 `IsEmptySlot()` 可区分空位，`GetSlotIndex()` 从 0 开始，`GetUnitId()` 在空位时返回无效 GUID；`SetEmptySlot(Index)` 将卡片变为空位并解除原单位订阅。内部 `MemberCards` 数组和 `MemberList` 包含实员与空位，不能再把控件数量直接当作成员人数。

草稿中第一位加入的成员自动成为队长。移除草稿队长时按成员加入顺序递补；清空草稿成员后队长 ID 变为无效。保存空队后正式小队仍然保留。移出成员不会删除单位数据或销毁单位 Pawn。

单位资料与小队列表通过管理类和单位修改事件刷新；编辑中的草稿不会被外部修改静默覆盖。小队解散或离开当前瓦片时结束该次编辑并返回名册，其他冲突由保存时的原始快照校验发现。刷新在后续 Tick 合并执行：小队行、人员行按 ID 复用，成员卡按席位索引复用，容量变化时只增删末尾差额并保持滚动位置，不清空重建整张列表。人员被移出后后续成员向前排列，仅受影响的席位换绑。成员卡保持同一单位智能指针时不重绑，也不取消尚在两帧延迟中的有效按钮点击；换人或变为空位时取消旧按钮的延迟点击，避免作用到另一个人员。

## 队徽

预设纹理位于主界面目录的 `Icons`：

| 纹理 | 预设 |
| --- | --- |
| `T_Squad_Raven` | 渡鸦 |
| `T_Squad_Wolf` | 狼 |
| `T_Squad_Northstar` | 北极星 |
| `T_Squad_Shield` | 盾徽 |

预设队徽目录已列入 `DefaultGame.ini` 的 `DirectoriesToAlwaysCook`，保证通过配置软引用的四个队徽进入打包内容。本轮未执行完整打包验证。

在 **项目设置 → SilverChoir → 玩家小队配置 → 预设小队图标** 修改或追加图标。选择器按 `PresetSquadIcons` 数组顺序显示；无效资产路径会跳过。图标选项在控件构造时读取，配置改变后重新打开界面。

点击右上管理面板的队徽，在队徽左侧打开 280×300 UMG 单位的 `IconPickerPanel` 小面板，顶部与队徽对齐，保留 10 单位间距。四个预设按 2×2 排列，下方有独立的 **使用队长头像作为标识** 按钮；没有草稿队长时禁用头像选项。选择只更新草稿预览，仍需点击底部 **保存小队**。再次点击队徽、点击关闭或选中图标会收起面板，不丢弃当前草稿。

小面板跟随队徽位置，窗口变化时限制在界面可见范围内，左侧空间不足时尝试右侧，再按屏幕边界调整；非常小的窗口按比例缩小。面板没有全屏遮罩，仅自身区域接收命中，预设区域支持滚动。`IconPickerPanel` 是铺满根画布且自身不参与命中的容器；`IconPickerFit` 必须保持 Canvas 槽、零锚点和零对齐，由 C++ 更新位置与尺寸；`IconPickerExtent` 提供设计尺寸。

`FSquadData.IconSource` 区分预设队徽和队长头像。选择队长头像时，显示图标通过队长单位 ID 查询权威单位数据，不复制或缓存一份队长头像。更换队长或修改其头像后，展示会更新。队长没有头像或小队为空时回退到 `SquadIcon` 中保留的预设队徽；选择预设图标会重新切回预设来源。

## 车辆选择与缩容

车辆现在统一存储在玩家单位子系统中。右侧显示已选车辆的 16:9 图片、车型和座位数，未选时显示默认 4 人。点击 **选择车辆** 即从玩家单位管理类读取当前瓦片的已有车辆，打开右侧管理区内的选择面板，无需在 UI 蓝图中维护另一份车辆数组。

建议的蓝图接入顺序：

1. 在新游戏初始化中调用玩家单位函数库的 **从模板表加载玩家车辆**，传入 `FVehicleTemplate` 表与实际的 `TileId`。该节点每行创建一辆新车并加载到管理类，返回新车辆 ID；每次执行都会生成新实例，不要放进每次打开 UI 的事件中。
2. 恢复存档时使用 **加载玩家车辆数据数组**，保留车辆原有 ID；按 ID 合并并原地更新，不会清空未包含的车辆。
3. 创建会议室 UI 后调用 **展示小队列表**，传入当前瓦片。车辆列表默认沿用此位置，无需额外接线。
4. 若需要单独更新车辆筛选位置，调用会议室实例的 **加载当前瓦片车辆列表** `LoadAvailableVehicles(TileId)`。传入 `None` 返回失败，并保留先前有效的筛选位置。

模板表可使用 `/Game/System/Data/Vehicles/DT_VehicleTemplates`。模板与存档加载节点操作玩家车辆记录，UI 只查询这些已有记录。车辆模板、实例与实体基类见 `Docs/VehicleData.md`。

**请求选择小队车辆** `OnVehicleSelectionRequested(SquadId)` 保留为打开选择框前的可选通知，不再要求在其中提供数据。尚未保存的新队没有正式 ID，此时参数仍为无效 Guid。通知可以执行附加业务；如果它关闭了 UI 或改选了小队，原调用不会继续打开旧上下文的选择框。

| 车辆蓝图节点 | 行为 |
| --- | --- |
| 加载当前瓦片车辆列表 `LoadAvailableVehicles` | 输入 `TileId`，从玩家单位子系统重新读取此位置的车辆；成功返回 `true`，空瓦片列表也是成功 |
| 获取可选车辆列表 `GetAvailableVehicles` | 返回当前筛选瓦片的权威车辆快照；修改返回数组不会写回车辆管理类 |
| 选择列表中的小队车辆 `SelectAvailableVehicle` | 输入车辆 ID，重新查询权威数据和当前位置、正式归属；仅修改草稿并关闭选择框 |
| 显示小队车辆选择框 `SetVehiclePickerVisible` | 输入是否显示，仅在允许编辑的管理层打开 |
| 小队车辆选择框是否显示 `IsVehiclePickerVisible` | 查询选择框状态 |
| 设置可选车辆列表 `SetAvailableVehicles`（已弃用） | 只校验输入 ID 有效、不重复且已登记，然后刷新当前瓦片全部车辆；不导入记录、不限定子集，传空数组也不会清空玩家车辆或当前瓦片的列表 |

车辆卡显示名称、座位（含驾驶席）、当前瓦片与选择状态。**不在传入瓦片的车辆直接不显示**，位置未设置的车辆也不会匹配有效瓦片。当前瓦片没有车辆时显示“当前瓦片暂无车辆”。同瓦片但已被其他正式小队占用的车辆保留显示并禁用；当前小队自己的车辆可以选择。点击时再次校验车辆 ID、位置及正式归属，防止列表打开后车辆移动、被移除或分配给其他小队。负座位数据在加载到管理类时被拒绝；界面仍保留防御检查。

当前选车会高亮。车辆选择框与队徽选择框互斥，顶部关闭按钮只收起面板，不清除草稿。右侧 **取消车辆** 按钮调用原有 **清除当前小队车辆**，恢复默认四人容量。原有 **设置当前小队车辆** 仍接受一条结构体记录，但只使用其中的 ID 查询权威数据，不会用传入快照覆盖管理类中的位置或座位；未登记 ID 会失败。不要以无效 ID 冒充清除操作。

仅改变 UI 筛选瓦片不会自动清除当前草稿车辆；若当前选择不在该瓦片，面板会提示选择保持不变。车辆的实际删除、移动和正式归属变化由管理类协调，保存时再次校验，UI 筛选不能绕过同瓦片和占用限制。需要重新应用已选车辆的最新参数时，可以再次选择同一 ID。

会议室与车辆卡持有管理类提供的 `TSharedPtr<FVehicleData>`。会议室监听管理类的车辆和小队修改事件，在后续 Tick 合并刷新；按 ID 复用已有卡片，不每帧扫描车辆或小队。车辆进入当前瓦片时加入列表，离开时移除；其他小队取得或释放车辆后更新禁用状态。数据通知会立即取消尚未触发的延迟点击，实际选择仍重新查询管理类。界面销毁时移除通知订阅并释放引用。卡片与主面板保存在原 Designer 中，本轮只更换数据来源，无需重建布局或动画。

设置节点预检车辆 ID、同瓦片、座位非负和车辆未被其他正式小队占用；错误通过返回值、`LastError` 与 `OnOperationFailed` 提示，不改变草稿。成功设置后容量立即按车更新。若容量小于当前成员数，优先保留队长及最早加入者，从末尾移出其他成员；零座时全部移出并清空草稿队长。移出列表通过 `OutRemovedUnitIds` 返回，蓝图可用它显示提示。清除车辆恢复 4 人时采用同一规则。

上述移出只发生在界面草稿中，正式单位归属、车辆分配及其他小队在保存前不变。点击 **保存小队** 后管理类再次校验完整草稿，统一提交车辆、人数和成员归属；校验失败保留草稿。返回列表或关闭界面会一起放弃选车、清车和缩容移出的修改。

选择导致缩容时，车辆区会持续提示从草稿移出的成员数量；保存前可重新选大车并手动将人员加入，已移出的人员不会因为扩大容量自动加入。

## 动画与生命周期

`WBP_SquadMeetingRoom` 的 Designer → 动画中包含八条可编辑 UMG 动画。C++ 选择当前层级对应的动画，并等待这一阶段所有动画的 Finished；修改时间轴长度后无需同步修改 C++ 固定延时。

| 动画 | 对象与方向 | 默认节奏 |
| --- | --- | --- |
| 小队列表滑入 / 滑出 | `LeftPanel`，从左侧进入、向左退出 | 入 0.30 秒，出 0.22 秒 |
| 人员列表滑入 / 滑出 | `LeftPanel`，与小队列表独立的时间轴 | 入 0.30 秒，出 0.22 秒 |
| 管理面板滑入 / 滑出 | `ManagementPanel`，从右侧进入、向右退出 | 入场延迟 0.04 秒，再播放 0.30 秒；出 0.22 秒 |
| 成员名单滑入 / 滑出 | `MemberStripPanel`，从下方进入、向下退出 | 入场延迟 0.08 秒，再播放 0.30 秒；出 0.22 秒 |

动画使用短距离位移和淡入淡出，入场减速、退场加速；左右分别偏移 72/80 UMG 单位，底部偏移 40 单位。没有把滑动距离写成某一种屏幕的整个宽度。进入帧透明度为零，延迟期间保持隐藏；动画结束保留目标状态。可直接调整 `RenderTransform` / `RenderOpacity` 的关键帧、切线和时间范围。建议保留进入终点的位置为零、透明度为一，退出终点透明度为零。

选择小队时先播放“小队列表滑出”，结束后切换列表，再同时播放人员、管理面板和成员名单的滑入动画；返回列表先等待当前三块滑出，再播放小队列表滑入。关闭界面仅退出当前层级实际显示的面板。

类默认值的 **小队 → 动画** 中可以替换八个动画名称、关闭 **使用蓝图动画**，或调整 **蓝图动画播放速度**。蓝图节点 **获取小队面板动画** 可取得当前左侧列表、管理面板或成员名单的进入/退出动画。不要在默认自动播放流程中再重复调用 Play Animation。

`TransitionDuration` 现为 **回退过渡时长（0禁用动效）**：缺少某阶段动画、关闭蓝图动画，或关闭界面打断正在进行的动画时使用，默认 `0.22` 秒。正常 UMG 播放不受这个固定时长限制。设为零可禁用全部过渡，但加载/卸载仍至少经过一次 Tick 再通知，方便调用方绑定完成事件。动画被打断关闭时先解绑旧 Finished，再从当前位移和透明度继续平滑退出，避免跳回动画首帧。

默认 `LoadSceneUI` 动画完成后调用 **通知加载完成**；默认 `UnloadSceneUI` 动画完成后调用 **通知卸载完成**，由 BaseWidget 移除控件。因此使用默认动画时只需监听 `OnLoadCompleted`、`OnUnloadCompleted`，不需要额外手动通知，也不要在退场开始时直接 `RemoveFromParent`。

如要完全使用蓝图动画，可重载继承的 **加载场景UI**／**卸载场景UI**，自己播放动画，并在 `OnAnimationFinished` 中手动调用对应的 **通知加载完成**／**通知卸载完成**。完全接管时不要再调用父实现启动另一套默认完成计时；退场动画期间也应由蓝图禁用交互。仅在 **场景UI打开/关闭** 事件中添加动画不会取消 C++ 默认动画和完成时机。

**小队管理层级已切换** `OnLayerChanged(Layer)` 在内部层级动画结束后触发，可处理附加展示逻辑；它不是加载/卸载完成事件。**小队操作失败** `OnOperationFailed(Error)` 提供业务失败提示，也会写入 `LastError` 和界面提示区域。

## 数据唯一性与蓝图编辑约束

单位权威数据仍只在玩家单位管理类的 `TMap<FGuid, TSharedPtr<FUnitData>>` 中保存一份。小队只持有成员 ID；人员行与队员卡通过管理类获取同一份 `TSharedPtr<FUnitData>`，绑定 `OnDataChanged`，在换绑、销毁时移除订阅并释放指针。不要为此界面另建可修改的 `FUnitData` 缓存数组。

车辆权威数据同样只在玩家单位管理类的 `TMap<FGuid, TSharedPtr<FVehicleData>>` 中保存一份。车辆列表与卡片持有相同共享指针；会议室集中监听管理类通知并更新卡片。复制结构体仅用于蓝图查询、存档与编辑快照，不会复制修改事件或自动写回；C++ 修改权威记录应使用 `Modify` / `NotifyDataChanged`。

小队快照 `FSquadData` 对单位只保存成员 ID，车辆使用 `AssignedVehicle` 作为显示与草稿兼容记录；正式关联和占用以车辆 ID 为准。会议室通过自己的编辑节点修改草稿，最终由 `CommitSquadDraft` 校验并提交；`GetSelectedSquad` 返回快照的修改不会自动写回草稿或管理类。其他游戏业务直接调用小队管理类的即时修改接口时，仍按各接口原有规则立即生效。

可在 UMG Designer 编辑布局与样式，但保留 C++ 绑定的控件名称和兼容类型，尤其是 `LeftPages`（第 0 页小队、第 1 页人员）、`LeftPanel`、`ManagementPanel`、`MemberStripPanel`、`SquadList`、`PersonnelList`、`MemberList`、`IconOptions`。不要让全屏装饰层拦截按钮或输入框的命中测试。

草稿布局新增绑定：`SaveSquadButton`、`ChooseSquadIconButton`、`CloseIconPickerButton` 均为 `UBasicButtonWidget`；`IconPickerPanel` 为面板，`DraftStatusText` 为文本。`SquadNameInput`、`SquadLocationText`、`SquadIconImage` 沿用原名称。队徽图片必须设为 `HitTestInvisible`，由其下方的 `ChooseSquadIconButton` 接收点击。原 `SaveSquadNameButton` 保留但折叠，不再作为正式保存入口。

车辆文案绑定沿用 `VehicleText`，并增加可选的 `VehicleHint` 文本绑定，用于运行时显示座位包含驾驶席、保存后生效的提示。本轮容量与空位卡改造复用现有蓝图布局，无需重建 Designer 控件树或修改用户动画。

`SquadUIAssets -Inspect` 只读检查界面、现有场景图表以及小队/人员整备室动画轨道；`-Build` 用于创建缺失布局与组件；`-Animations` 只补齐上述八条缺失动画，保留已有同名动画、布局和业务图表。这些工具不是运行时依赖。资产保存前的副本放在 `Saved/SquadUIBackups`，不要使用其他界面的重建命令代替本界面的维护工具。

`SquadSceneFlow -Inspect` 检查小队场景业务图；`-Build` 是本轮场景进入/退出图表的生成维护工具，会重建它负责的生命周期函数，**不要在手动扩展这些函数后无备份地重复运行**。生成前保存原小队场景到 `Saved/SquadSceneFlowBackups/<时间>/`。日常调整直接在场景蓝图编辑器中进行即可。

`SquadDraftUIAssets -Build` 是此次右侧草稿布局的单次补丁工具：迁移原输入框和图标选项，保留原图表、动画绑定、主要面板位置及车辆占位；修改前备份到 `Saved/SquadDraftUIBackups/<时间与唯一ID>/`。已存在新控件时拒绝重复插入，使用 `-Inspect` 只读检查控件树和动画。它不会替代场景业务蓝图，也不在运行时使用。

`SquadCompactUIAssets -Build` 调整上述队徽小面板与紧凑成员卡，并将成员条高度增加到 284。保存前备份两份蓝图至 `Saved/SquadCompactUIBackups/<时间与唯一ID>/`，校验图表、动画和其他主要面板位置；`-Inspect` 只读查看布局。日常美术调整在 Designer 中完成，避免重复运行工具覆盖后来手动调整的样式。

`SquadVehicleUIAssets -Build` 为已有会议室增补车辆选择器、车辆预览和 `Components/WBP_SquadVehicleEntry`，已经补齐时只验证，不重建用户布局；`-Inspect` 只读检查。车辆相关绑定还包括 `VehiclePickerPanel`、`VehicleOptions`、`CloseVehiclePickerButton`、`ClearVehicleButton`、`VehiclePreviewImage`、`VehicleEmptyText`、`VehiclePickerStatus`，`VehicleEntryClass` 默认指向上述条目蓝图。保存前备份到 `Saved/SquadVehicleUIBackups/<时间与唯一ID>/`，并检查原图表、动画和主要面板位置。

车辆图片由 ScaleBox 保持 16:9，内部 Image 的槽应水平、垂直居中，不要设为 Fill；`SquadVehicleUIAssets -FixPreviewAspect` 是仅修正这两处图片槽对齐的维护补丁，也会先备份并校验其余布局。上述工具都不是运行时依赖；日常修改样式直接编辑两份控件蓝图即可。

## 既有验证记录（草稿改造前，2026-09-27）

以下日志和截图对应初版界面、动画与场景生命周期，不能代替此次草稿保存及新布局的验证结果。

- `SilverChoirEditor Win64 Development` 完整编译成功；主界面和三个组件蓝图编译并保存成功。
- 编辑器测试四组通过：`Membership`、`Persistence`、`CaptainAndIcons`、`SharedUnitData`。日志：`Saved/Logs/SquadCoreTests.log`。
- 游戏进程测试四组通过：`Squads.Subsystem`、`SquadMeetingRoom.DataAndControls`、`SquadMeetingRoom.TransitionsAndCapture`、`SquadMeetingRoom.InterruptedTransitions`。日志：`Saved/Logs/SquadUIRuntimeTests.log`。
- 最后一项覆盖动画中外部解散当前小队、返回列表途中关闭、入场途中关闭；均正确返回列表或完成卸载、释放宿主和事件绑定。
- 实际渲染检查：1920×1080（16:9）、1440×900（16:10）、1280×960（4:3）、2560×720（32:9）。截图位于 `Saved/Screenshots/SquadUI`。左右面板、队徽、成员卡和操作按钮没有重叠或裁切；长名单使用滚动区域。
- 截图使用独立测试进程的临时人员和小队，没有将测试数据写入正式存档。该验证直接通过 BaseWidget 挂载界面，不包含场景蓝图中另行编辑的相机与场景业务流程。

### UMG 动画补充验证

- 编译通过；八条动画保存后在新的编辑器命令进程中重新加载、校验绑定及变量 GUID，结果 `SQUAD_UI_ANIMATIONS_OK added=0 count=8`，无动画相关 ensure。日志 `Saved/Logs/SquadAnimationsVerify.log`。命令进程仍会因原有 EasyHouseBuilderEditor 的 `SetWallOpenings is not AICallable` 工具注册错误返回非零，该错误不属于动画校验。
- 游戏进程中 `BlueprintAnimations`、`DataAndControls`、`InterruptedTransitions`、`TransitionsAndCapture` 四项测试通过，退出码 0。日志 `Saved/Logs/SquadAnimationRuntimeTests.log`。
- `BlueprintAnimations` 实际将动画降至半速，检查位置/透明度变化、播放超过 0.22 秒仍未提前通知、右侧和底部错峰结束前保持交互锁，以及完成后再卸载。
- 打断测试检查动画已有位移时立即关闭，位置与透明度在停止旧动画时保持连续；仍覆盖外部解散小队、入场中关闭及事件解绑。
- 本轮截图 `Saved/Screenshots/SquadUI/Animations16x9_02_Management.png` 检查了最终布局。原场景蓝图、人员整备室资产及原界面布局未由动画生成步骤重建。

### 场景进入/退出补充验证

- 完整 C++ 编译成功；`BP_小队会议室_Scene` 编译、保存成功，日志 `Saved/Logs/SquadSceneFlowBuild.log` 中结果为 `SQUAD_SCENE_FLOW_OK`。命令进程仍因前述 EasyHouseBuilderEditor 工具注册错误返回非零，没有小队场景编译错误。
- 新游戏进程重新读取保存的蓝图，`SilverChoir.BaseUI.SquadSceneFlow.NormalLifecycle`、`MissingDependencies` 两项通过，进程退出码 0。日志 `Saved/Logs/SquadSceneFlowRuntimeTests.log`。
- 覆盖真实蓝图“小队会议室 → 人员整备室 → 小队会议室 → 基地全景”，包括已加载 UI 的复用、相机先结束而 UI 尚未结束、卸载回调中不提前通知、自然退场、重复切换拒绝，以及事件解绑和旧引用释放。
- 验证未注册相机、玩家控制器没有基地 UI 时可记录异常并正常退出。测试使用独立管理器、临时瓦片和临时相机目标，未保存测试值到资产；原小队场景的两段相机位置保持不变。
- 此次测试直接运行主地图中的场景业务，不包含从主菜单开始的地图加载过程；没有执行完整打包。

## 草稿保存改造验证（2026-09-27）

- C++ 完整编译通过；`SquadDraftUIAssets -Build` 备份并保存现有 WBP，图表、主要面板位置和八条动画绑定指纹保持一致。日志：`Saved/Logs/SquadDraftUIBuild.log`，成功标记 `SQUAD_DRAFT_UI_OK`。命令进程仍有工程原有的 EasyHouseBuilderEditor 工具注册报错。
- 小队核心七项测试通过，包括四项新增草稿事务测试：新建失败不留记录、跨队原子提交、过期快照拒绝、共享单位数据与修改通知一致性。日志：`Saved/Logs/SquadDraftCoreTests.log`。
- 会议室四项运行测试全部通过：`BlueprintAnimations`、`DataAndControls`、`InterruptedTransitions`、`TransitionsAndCapture`。日志：`Saved/Logs/SquadDraftFinalRuntime.log`，进程退出码 0。
- 覆盖保存前不更改权威数据、取消新建、现有编辑返回/卸载丢弃、保存时才转队、失败保留草稿，以及队徽弹窗开关。使用真实溢出列表验证 48px 非零滚动位置和人员行对象保持不变。
- 1920×1080 与 1280×960 实际渲染检查通过。截图：`Saved/Screenshots/SquadUI/Draft16x9_*` 与 `Draft4x3_*`。测试主地图未加载基地室内子地图，因此截图中间模型区域为黑色；没有修改基地关卡。
- 当前用户动画等长约 0.25 秒；原测试硬编码了较长的右侧/底部错峰时长，已改为按实际时间轴验证全部动画完成后解锁，未改动用户的动画节奏。

## 紧凑成员卡与侧边队徽面板（2026-09-27）

- C++ 完整编译通过；两份控件蓝图编译、保存成功。布局工具校验图表、动画及其他主要面板位置保持一致，结果为 `SQUAD_COMPACT_UI_OK`，日志 `Saved/Logs/SquadCompactUIBuild.log`。命令进程仍因工程原有的 EasyHouseBuilderEditor 工具注册错误返回非零。
- 1920×1080 下五项会议室运行测试通过，含新增 `CompactMembersAndPicker`：六张成员卡同时完整显示且无需横向滚动，队长使用暗金色，队徽面板位于右侧管理区旁且不越界；原草稿保存和动画测试仍通过。日志 `Saved/Logs/SquadCompactRuntime.log`。
- 成员卡在 ScrollBox 中使用顶部对齐，保持 122×204 设计尺寸，避免默认 Fill 将卡片纵向拉伸。
- 1280×960 的专项运行测试通过，队徽面板保持在可见范围内，名单可横向滚动至第六人且完整显示。测试分别在列表开头和末尾检查可见卡片，覆盖六名成员，避免读取被 Slate 裁剪的离屏子控件旧坐标。日志 `Saved/Logs/SquadCompact4x3Runtime.log`，截图 `Saved/Screenshots/SquadUI/Compact4x3_Compact_*`。
- 实测截图位于 `Saved/Screenshots/SquadUI/Compact16x9_Compact_*`。使用临时单位与小队；测试地图未加载室内子关卡，所以中间场景区域为黑色。

## 保存后返回列表（2026-09-27）

功能代码完整编译后，`BlueprintAnimations`、`DataAndControls`、`InterruptedTransitions`、`SaveNavigation`、`TransitionsAndCapture` 五项运行测试通过，日志 `Saved/Logs/SquadSaveReturnRuntime.log`。覆盖提交后返回、失败保留编辑、实际 `OnSquadSaved` 回调改选/卸载时不被旧保存流程覆盖，以及原草稿语义和动画。

`CompactMembersAndPicker` 已改为等待 `bTransitioning` 结束再验证保存返回的最终状态，专项复验通过，日志 `Saved/Logs/SquadSaveReturnFinalRuntime.log`。至此六项会议室运行测试均通过。最终完整链接成功；临时占用 DLL 的外部程序已由用户关闭，无未完成的文件锁问题。

## 容量、车辆草稿与空位卡验证（2026-09-28）

本轮 `SilverChoirEditor Win64 Development` 完整编译、链接成功，日志 `Saved/Logs/SquadVehicleCapacityBuild.log`。

1920×1080 下七项会议室运行测试全部通过，含新增 `VehicleCapacity` 和扩展后的 `SaveNavigation`；同进程中的小队函数库测试及四项人员整备室返回测试也通过。日志 `Saved/Logs/SquadVehicleCapacityRuntimeTests.log`。覆盖默认四席、实员与空位切换、满员及单位通知后继续禁选、零座车辆、扩缩容、队长保留、取消不改正式数据、保存后提交、席位对象复用和共享单位指针保持，以及同步保存通知改选、跨草稿延迟点击取消。

已检查截图 `Saved/Screenshots/SquadUI/Capacity_01_Default4.png` 和 `Capacity_02_SixSeats_TwoMembers.png`：分别为一人加三个空位、两人加四个空位，1920×1080 下六格完整显示。截图使用临时测试数据；测试世界未加载基地室内子关卡，中间区域为黑色。

联合运行中的旧 `SilverChoir.BaseUI.Personnel` 假设进程仅含其八个测试单位，受会议室滚动测试留下的二十二个记录影响，出现人数断言失败。未更改业务来迎合该断言；在全新进程单独运行该测试已通过，日志 `Saved/Logs/SquadVehiclePersonnelIsolated.log`。共享人员行的瓦片限制与档案通知功能通过独立复验。

## 既有蓝图供数与车辆选择器验证（2026-09-28，迁入玩家单位子系统前）

以下为上一版本的历史记录；其“蓝图提供候选数组、异地显示禁用”行为已由上文的子系统读取和异地隐藏替代，不能作为本轮数据来源改造的验证结果。

- 完整 C++ 编译、链接成功，最终日志 `Saved/Logs/SquadVehiclePickerAspectBuild.log`。会议室与车辆条目蓝图编译保存成功，日志 `SquadVehiclePickerAssetsBuild.log`、`SquadVehiclePickerAspectAssets.log` 分别记录 `SQUAD_VEHICLE_UI_OK` 和 `SQUAD_VEHICLE_UI_ASPECT_OK`。资产命令进程仍受工程原有编辑器启动报错影响返回非零；保存结果随后由全新游戏测试进程重新加载验证。
- 原有七项会议室运行测试全部通过：动画、紧凑成员卡/队徽选择器、数据与控件、中断过渡、保存返回、层级过渡和车辆容量。日志 `Saved/Logs/SquadVehiclePickerRuntime.log`。
- 新增 `SilverChoir.BaseUI.SquadMeetingRoom.VehiclePicker` 检查空来源、无效/重复 ID 原子拒绝、异地/占用禁选、打开后占用变化、过期延迟点击取消、条目复用、同 ID 最新参数、2/4/6 席、取消与保存语义、新建草稿取消、清除车辆、选择框互斥及卸载。
- 首轮该专项测试发现图片被 ScaleBox 的 Fill 槽拉伸；修正资产对齐后，1920×1080、1280×720、1280×960 三个独立游戏进程全部通过，退出码均为 0。对应日志 `Saved/Logs/VehiclePicker_1920x1080.log`、`VehiclePicker_1280x720.log`、`VehiclePicker_1280x960.log`。验证包含实际图片几何比例、选择框边界及滚动区域。
- 实际渲染截图位于 `Saved/Screenshots/SquadUI/VehiclePicker_*_01_Choices.png` 和 `VehiclePicker_*_02_SelectedPreview.png`。已检查短窗口与 4:3 布局，车辆列表可滚动，保存按钮保持可见。测试使用临时车辆和小队，未加载基地室内子关卡或创建测试人员，因此中间黑色、人员列表为空；不向正式资产写入测试数据。
- 当时生产界面由蓝图提供车辆数组；当时测试读取车辆模板仅用于临时夹具。该轮没有执行完整打包，也未重新测量整场景 CPU/GPU 性能。

## 车辆迁入玩家单位子系统验证（2026-09-28）

本轮完整编译通过，日志 `Saved/Logs/PlayerVehicleStoreBuild.log`。17 项数据测试和 10 项运行测试全部通过，进程退出码均为 0；日志分别为 `PlayerVehicleStoreCoreTests.log` 和 `PlayerVehicleStoreRuntimeTests.log`。运行测试重新加载现有 WBP，覆盖同瓦片来源、异地隐藏、占用禁选、实时移入/移出、草稿保存/取消、容量、动画与解绑；布局和动画资产未重建。具体节点与验证范围见 [PlayerVehicles.md](PlayerVehicles.md)。

## 转队确认与默认名称表验证（2026-09-28）

- `SilverChoirEditor Win64 Development` 完整编译、链接通过，日志 `Saved/Logs/SquadTransferNamesBuild.log`。
- 名称表已生成 24 行并配置到玩家小队设置；会议室蓝图新增确认面板，原图表、动画绑定及主要面板位置保持不变。日志 `Saved/Logs/SquadTransferNamesAssets.log` 包含 `SQUAD_NAMES_TABLE_CREATED rows=24` 与 `SQUAD_TRANSFER_UI_OK`。资产命令进程仍因工程既有 EasyHouseBuilderEditor 的 `SetWallOpenings is not AICallable` 注册错误返回非零；保存后的资产已由全新游戏测试进程成功重新加载。
- 14 项核心数据测试通过，日志 `Saved/Logs/SquadTransferNamesCoreTests.log`。新增名称测试覆盖排序、同排序行名顺序、空白及重复名称、名称占用与释放、取消不占名、名称耗尽后的编号、缺失或错误表的回退。
- 1920×1080 下九项会议室运行测试通过，日志 `Saved/Logs/SquadTransferNamesRuntimeTests.log`。新增 `TransferConfirmation` 覆盖原小队名称与暗金色显示、取消、确认后仅改草稿、保存后正式转队、共享单位指针保持、人员行复用、原队重命名、确认前后归属变化、异地及卸载失效、新建草稿默认名及取消。
- 1280×960 的转队专项测试通过，日志 `Saved/Logs/SquadTransfer4x3Tests.log`。已人工检查实际渲染截图 `Saved/Screenshots/SquadUI/Transfer1920_01_Confirm.png`、`Transfer1920_02_Draft.png` 和 `Transfer4x3_01_Confirm.png`：确认文本、按钮无重叠或裁切，保存前原队名及“待调入”提示正确显示。
- 全新进程中的人员整备室及四项返回流程测试全部通过，日志 `Saved/Logs/SquadTransferPersonnelTests.log`；共享人员行未影响原有人员档案与返回流程。
- 测试使用临时管理器和数据，未写入正式存档；截图未加载基地室内子关卡，因此中央展示区域为黑色。自动化调用实际按钮事件并验证业务及布局，没有模拟完整鼠标命中或键盘焦点路径；未执行完整打包。
